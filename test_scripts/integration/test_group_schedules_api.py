import urllib.request
import urllib.error
import json
import os
import sys
import ssl
import uuid

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
TOKEN = "dummy-test-token"

HTTPS_CONTEXT = ssl._create_unverified_context()

def open_url(req_or_url):
    url = req_or_url.full_url if hasattr(req_or_url, "full_url") else req_or_url
    if str(url).startswith("https://"):
        return urllib.request.urlopen(req_or_url, context=HTTPS_CONTEXT)
    return urllib.request.urlopen(req_or_url)

def set_scenario(scenario_name):
    req1 = urllib.request.Request(f"{FAKE_URL}/reset-observations", data=b"", method="POST")
    open_url(req1)
    req2 = urllib.request.Request(
        f"{FAKE_URL}/set-scenario",
        data=json.dumps({"scenario": scenario_name}).encode(),
        method="POST"
    )
    open_url(req2)

def reset_db():
    req = urllib.request.Request(f"{FAKE_URL}/reset-db", data=b"", method="POST")
    open_url(req)

def request(method, path, body=None, headers=None, scenario="normal"):
    set_scenario(scenario)
    if headers is None:
        headers = {"Authorization": f"Bearer {TOKEN}"}
    if body is not None:
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(f"{USERPORTAL_URL}{path}", data=body, headers=headers, method=method)
    try:
        with open_url(req) as response:
            res_body = response.read()
            try:
                return response.status, json.loads(res_body) if res_body else {}
            except json.JSONDecodeError:
                return response.status, res_body
    except urllib.error.HTTPError as e:
        res_body = e.read()
        try:
            return e.code, json.loads(res_body) if res_body else {}
        except Exception:
            return e.code, res_body

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def get_observations():
    with open_url(f"{FAKE_URL}/observations") as r:
        return json.loads(r.read())

def create_group(name="test-group"):
    status, body = request("POST", "/api/v1/groups", body={"name": name, "description": "desc"})
    assert status == 200, f"Expected 200 on group create, got {status}. Body: {body}"
    return body["id"]

def create_schedule(name="test-schedule"):
    payload = {
        "name": name,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1, 2, 3]
    }
    status, body = request("POST", "/api/v1/schedules", body=payload)
    assert status == 200, f"Expected 200 on schedule create, got {status}. Body: {body}"
    return body["id"]


# ---------------------------------------------------------------------------
# Happy-path Read-after-Write CRUD Lifecycle & State Verification
# ---------------------------------------------------------------------------

def test_group_schedule_crud_state_lifecycle():
    print("Testing Group Schedule CRUD State Lifecycle (Read-after-Write)...")
    gid = create_group("gs-crud-lifecycle")
    sid1 = create_schedule("gs-sched1")
    sid2 = create_schedule("gs-sched2")

    # 1. Initially list should be empty
    status, list_body = request("GET", f"/api/v1/groups/{gid}/schedules")
    assert status == 200, f"Expected 200, got {status}"
    assert len(list_body) == 0, f"Expected empty schedule list, got {list_body}"

    # 2. GET single schedule that does not exist -> 404 (State-based not found)
    status, _ = request("GET", f"/api/v1/groups/{gid}/schedules/{sid1}")
    assert status == 404, f"Expected state-based 404 for unlinked schedule, got {status}"

    # 3. POST link schedule -> 200
    status, post_body = request("POST", f"/api/v1/groups/{gid}/schedules",
                               body={"schedule_id": sid1},
                               scenario="config-raw")
    assert status == 200, f"Expected 200 on POST, got {status}"
    assert "config-raw" not in post_body, "config-raw should be stripped from response"

    # 4. GET list should show linked schedule
    status, list_body = request("GET", f"/api/v1/groups/{gid}/schedules")
    assert status == 200
    assert any(s.get("id") == sid1 for s in list_body), f"Linked schedule {sid1} not found in list: {list_body}"

    # 5. GET single schedule should succeed
    status, get_body = request("GET", f"/api/v1/groups/{gid}/schedules/{sid1}")
    assert status == 200
    assert get_body.get("id") == sid1, f"Expected schedule ID {sid1}, got {get_body}"

    # 6. PUT replace with both schedules -> 200
    status, put_body = request("PUT", f"/api/v1/groups/{gid}/schedules",
                               body={"schedule_ids": [sid1, sid2]},
                               scenario="config-raw")
    assert status == 200, f"Expected 200 on PUT, got {status}"

    # 7. GET list should reflect replacement
    status, list_body = request("GET", f"/api/v1/groups/{gid}/schedules")
    assert status == 200
    assert len(list_body) == 2, f"Expected 2 schedules, got {list_body}"
    assert any(s.get("id") == sid1 for s in list_body)
    assert any(s.get("id") == sid2 for s in list_body)

    # 8. DELETE unlink schedule -> 200
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/schedules/{sid1}",
                        scenario="config-raw")
    assert status == 200, f"Expected 200 on DELETE, got {status}"

    # 9. GET single schedule should be 404 (Real post-delete 404 flow)
    status, _ = request("GET", f"/api/v1/groups/{gid}/schedules/{sid1}")
    assert status == 404, f"Expected 404 after delete, got {status}"

    # 10. GET list should only contain sid2
    status, list_body = request("GET", f"/api/v1/groups/{gid}/schedules")
    assert status == 200
    assert len(list_body) == 1, f"Expected 1 schedule left, got {list_body}"
    assert list_body[0].get("id") == sid2

    print("✅ Group Schedule CRUD State Lifecycle passed")


# ---------------------------------------------------------------------------
# POST (link) — downstream failure scenarios
# ---------------------------------------------------------------------------

def test_post_group_schedule_missing_config_raw():
    print("Testing POST /groups/{id}/schedules missing config-raw → 500...")
    gid = create_group("gs-post-no-cr")
    sid = create_schedule("gs-no-cr-sched")

    # "normal" scenario: downstream returns 200 but no config-raw
    status, _ = request("POST", f"/api/v1/groups/{gid}/schedules",
                        body={"schedule_id": sid}, scenario="normal")
    assert status == 500, f"Expected 500 when POST response missing config-raw, got {status}"
    print("✅ POST missing config-raw → 500 passed")


def test_post_group_schedule_downstream_errors():
    print("Testing POST /groups/{id}/schedules downstream errors...")
    gid = create_group("gs-post-err")
    sid = create_schedule("gs-err-sched")

    status, _ = request("POST", f"/api/v1/groups/{gid}/schedules",
                        body={"schedule_id": sid}, scenario="pc-409")
    assert status == 409, f"Expected 409 for conflict, got {status}"

    gid = create_group("gs-post-gw-get-502")
    sid = create_schedule("gs-gw-get-502-sched")
    status, _ = request("POST", f"/api/v1/groups/{gid}/schedules",
                        body={"schedule_id": sid}, scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw GET failure, got {status}"

    gid = create_group("gs-post-gw-configure-502")
    sid = create_schedule("gs-gw-configure-502-sched")
    status, _ = request("POST", f"/api/v1/groups/{gid}/schedules",
                        body={"schedule_id": sid}, scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure, got {status}"

    print("✅ POST downstream error scenarios passed")


# ---------------------------------------------------------------------------
# PUT — missing config-raw & validation
# ---------------------------------------------------------------------------

def test_put_group_schedules_missing_config_raw():
    print("Testing PUT /groups/{id}/schedules missing config-raw → 500...")
    gid = create_group("gs-put-no-cr")
    sid = create_schedule("gs-put-no-cr-sched")

    # First link it
    request("POST", f"/api/v1/groups/{gid}/schedules",
            body={"schedule_id": sid}, scenario="config-raw")

    status, _ = request("PUT", f"/api/v1/groups/{gid}/schedules",
                        body={"schedule_ids": [sid]}, scenario="normal")
    assert status == 500, f"Expected 500 when PUT response missing config-raw, got {status}"
    print("✅ PUT missing config-raw → 500 passed")


# ---------------------------------------------------------------------------
# DELETE — missing config-raw and failure scenarios
# ---------------------------------------------------------------------------

def test_delete_group_schedule_missing_config_raw():
    print("Testing DELETE /groups/{id}/schedules/{sid} missing config-raw → 500...")
    gid = create_group("gs-del-no-cr")
    sid = create_schedule("gs-del-no-cr-sched")
    
    # First link it
    request("POST", f"/api/v1/groups/{gid}/schedules",
            body={"schedule_id": sid}, scenario="config-raw")

    # normal scenario → delete succeeds downstream but returns no config-raw
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/schedules/{sid}", scenario="normal")
    assert status == 200, f"Expected 200 when DELETE response missing config-raw, got {status}"
    print("✅ DELETE missing config-raw → 200 passed")


def test_delete_group_schedule_gw_failures():
    print("Testing DELETE /groups/{id}/schedules/{sid} gateway failure scenarios...")
    gid = create_group("gs-del-prov-502")
    sid = create_schedule("gs-del-prov-502-sched")
    request("POST", f"/api/v1/groups/{gid}/schedules",
            body={"schedule_id": sid}, scenario="config-raw")
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/schedules/{sid}",
                        scenario="delete-config-raw-prov-502")
    assert status == 500, f"Expected 500 for prov failure, got {status}"

    gid = create_group("gs-del-gw-get-502")
    sid = create_schedule("gs-del-gw-get-502-sched")
    request("POST", f"/api/v1/groups/{gid}/schedules",
            body={"schedule_id": sid}, scenario="config-raw")
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/schedules/{sid}",
                        scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw GET failure, got {status}"

    gid = create_group("gs-del-gw-configure-502")
    sid = create_schedule("gs-del-gw-configure-502-sched")
    request("POST", f"/api/v1/groups/{gid}/schedules",
            body={"schedule_id": sid}, scenario="config-raw")
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/schedules/{sid}",
                        scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure, got {status}"

    print("✅ DELETE gateway failure scenarios passed")


def test_forwarded_404_scenarios():
    print("Testing forwarded 404 (for non-existent group)...")
    sid = create_schedule("gs-404-sched")

    status, _ = request("GET", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/schedules")
    assert status == 404, f"Expected 404 for non-existent group GET list, got {status}"

    status, _ = request("POST", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/schedules",
                        body={"schedule_id": sid})
    assert status == 404, f"Expected 404 for non-existent group POST, got {status}"

    status, _ = request("PUT", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/schedules",
                        body={"schedule_ids": [sid]})
    assert status == 404, f"Expected 404 for non-existent group PUT, got {status}"

    status, _ = request("DELETE", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/schedules/{sid}")
    assert status == 404, f"Expected 404 for non-existent group DELETE, got {status}"
    print("✅ Forwarded 404 scenarios passed")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print("Starting group-schedules integration tests...")
    try:
        reset_db()
        test_group_schedule_crud_state_lifecycle()
        test_post_group_schedule_missing_config_raw()
        test_post_group_schedule_downstream_errors()
        test_put_group_schedules_missing_config_raw()
        test_delete_group_schedule_missing_config_raw()
        test_delete_group_schedule_gw_failures()
        test_forwarded_404_scenarios()
        print("🎉 All group-schedules integration tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
