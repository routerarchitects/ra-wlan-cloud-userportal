import urllib.request
import urllib.error
import json
import os
import sys
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
VALID_GROUP_ID = "11111111-1111-4111-8111-111111111111"
VALID_SCHEDULE_ID = "22222222-2222-4222-8222-222222222222"
INVALID_UUID = "12345"

HTTPS_CONTEXT = ssl._create_unverified_context()

def open_url(req_or_url):
    url = req_or_url.full_url if hasattr(req_or_url, "full_url") else req_or_url
    if str(url).startswith("https://"):
        return urllib.request.urlopen(req_or_url, context=HTTPS_CONTEXT)
    return urllib.request.urlopen(req_or_url)

def reset_observations():
    req = urllib.request.Request(f"{FAKE_URL}/reset-observations", data=b"", method="POST")
    open_url(req)

def request(method, path, body=None, headers=None):
    if headers is None:
        headers = {"Authorization": "Bearer dummy-test-token"}
    if body is not None:
        body = json.dumps(body).encode("utf-8")
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
        except json.JSONDecodeError:
            return e.code, res_body
    except urllib.error.URLError as e:
        print(f"Connection failed: {e}")
        return 000, {}

def check_no_observations(test_name):
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert len(obs["calls"]) == 0, f"{test_name}: Downstream unexpectedly called: {obs['calls']}"

def check_downstream_called(test_name):
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert len(obs["calls"]) > 0, f"{test_name}: Request was not forwarded downstream (no calls observed)"


# ---------------------------------------------------------------------------
# Auth rejection tests
# ---------------------------------------------------------------------------

def test_auth_checks():
    print("Testing Auth Rejections...")

    # GET list
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", headers={})
    assert status == 403, f"Expected 403 for missing auth on GET schedules, got {status}"
    check_no_observations("GET group-schedules missing auth")

    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on GET schedules, got {status}"
    check_no_observations("GET group-schedules bad auth")

    # POST
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_id": VALID_SCHEDULE_ID}, headers={})
    assert status == 403, f"Expected 403 for missing auth on POST schedules, got {status}"
    check_no_observations("POST group-schedules missing auth")

    # PUT
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": [VALID_SCHEDULE_ID]}, headers={})
    assert status == 403, f"Expected 403 for missing auth on PUT schedules, got {status}"
    check_no_observations("PUT group-schedules missing auth")

    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": [VALID_SCHEDULE_ID]},
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on PUT schedules, got {status}"
    check_no_observations("PUT group-schedules bad auth")

    # GET single
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}", headers={})
    assert status == 403, f"Expected 403 for missing auth on GET single schedule, got {status}"
    check_no_observations("GET single schedule missing auth")

    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}",
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on GET single schedule, got {status}"
    check_no_observations("GET single schedule bad auth")

    # DELETE
    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}", headers={})
    assert status == 403, f"Expected 403 for missing auth on DELETE schedule, got {status}"
    check_no_observations("DELETE schedule missing auth")

    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}",
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on DELETE schedule, got {status}"
    check_no_observations("DELETE schedule bad auth")

    print("✅ Auth tests passed")


# ---------------------------------------------------------------------------
# Local validation tests — none of these should reach downstream
# ---------------------------------------------------------------------------

def test_local_validation():
    print("Testing Local Request Validation...")

    # --- GET /groups/{group_id}/schedules ---

    # Invalid group UUID on GET list
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{INVALID_UUID}/schedules")
    assert status == 400, f"Expected 400 for invalid group UUID on GET schedules, got {status}"
    check_no_observations("GET group-schedules invalid group UUID")

    # Invalid group UUID on GET single schedule link
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{INVALID_UUID}/schedules/{VALID_SCHEDULE_ID}")
    assert status == 400, f"Expected 400 for invalid group UUID on GET schedule, got {status}"
    check_no_observations("GET group-schedule invalid group UUID")

    # Invalid schedule UUID on GET single
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{INVALID_UUID}")
    assert status == 400, f"Expected 400 for invalid schedule UUID on GET schedule, got {status}"
    check_no_observations("GET group-schedule invalid schedule UUID")

    # --- POST /groups/{group_id}/schedules ---

    # Missing schedule_id
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={})
    assert status == 400, f"Expected 400 for missing schedule_id, got {status}"
    check_no_observations("POST group-schedules missing schedule_id")

    # Null schedule_id
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_id": None})
    assert status == 400, f"Expected 400 for null schedule_id, got {status}"
    check_no_observations("POST group-schedules null schedule_id")

    # Non-UUID schedule_id
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_id": INVALID_UUID})
    assert status == 400, f"Expected 400 for non-UUID schedule_id, got {status}"
    check_no_observations("POST group-schedules non-UUID schedule_id")

    # Unknown field in body
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_id": VALID_SCHEDULE_ID, "extra": "bad"})
    assert status == 400, f"Expected 400 for unknown field on POST schedules, got {status}"
    check_no_observations("POST group-schedules unknown field")

    # Missing body entirely
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}/schedules",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method="POST"
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for POST with missing body, got {status}"
    check_no_observations("POST group-schedules missing body")

    # Malformed JSON
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}/schedules",
        data=b"{bad-json",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method="POST"
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for POST malformed JSON, got {status}"
    check_no_observations("POST group-schedules malformed JSON")

    # --- PUT /groups/{group_id}/schedules ---

    # Invalid group UUID on PUT
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{INVALID_UUID}/schedules",
                        body={"schedule_ids": [VALID_SCHEDULE_ID]})
    assert status == 400, f"Expected 400 for invalid group UUID on PUT schedules, got {status}"
    check_no_observations("PUT group-schedules invalid group UUID")

    # Missing schedule_ids
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={})
    assert status == 400, f"Expected 400 for missing schedule_ids on PUT, got {status}"
    check_no_observations("PUT group-schedules missing schedule_ids")

    # Null schedule_ids
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": None})
    assert status == 400, f"Expected 400 for null schedule_ids on PUT, got {status}"
    check_no_observations("PUT group-schedules null schedule_ids")

    # Non-array schedule_ids
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": VALID_SCHEDULE_ID})
    assert status == 400, f"Expected 400 for non-array schedule_ids on PUT, got {status}"
    check_no_observations("PUT group-schedules non-array schedule_ids")

    # Non-UUID entry in schedule_ids
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": [INVALID_UUID]})
    assert status == 400, f"Expected 400 for non-UUID entry in schedule_ids, got {status}"
    check_no_observations("PUT group-schedules non-UUID entry")

    # Duplicate UUIDs in schedule_ids
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": [VALID_SCHEDULE_ID, VALID_SCHEDULE_ID]})
    assert status == 400, f"Expected 400 for duplicate UUIDs in schedule_ids, got {status}"
    check_no_observations("PUT group-schedules duplicate UUIDs")

    # Unknown field in PUT body
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules",
                        body={"schedule_ids": [VALID_SCHEDULE_ID], "extra": "bad"})
    assert status == 400, f"Expected 400 for unknown field on PUT schedules, got {status}"
    check_no_observations("PUT group-schedules unknown field")

    # Missing body on PUT
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}/schedules",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method="PUT"
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for PUT with missing body, got {status}"
    check_no_observations("PUT group-schedules missing body")

    # Malformed JSON on PUT
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}/schedules",
        data=b"{bad-json",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method="PUT"
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for PUT malformed JSON, got {status}"
    check_no_observations("PUT group-schedules malformed JSON")

    # --- DELETE /groups/{group_id}/schedules/{schedule_id} ---

    # Invalid group UUID on DELETE
    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{INVALID_UUID}/schedules/{VALID_SCHEDULE_ID}")
    assert status == 400, f"Expected 400 for invalid group UUID on DELETE schedule, got {status}"
    check_no_observations("DELETE group-schedule invalid group UUID")

    # Invalid schedule UUID on DELETE
    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{INVALID_UUID}")
    assert status == 400, f"Expected 400 for invalid schedule UUID on DELETE schedule, got {status}"
    check_no_observations("DELETE group-schedule invalid schedule UUID")

    print("✅ Local validation tests passed")


if __name__ == "__main__":
    print("Starting group-schedules contract tests...")
    try:
        test_auth_checks()
        test_local_validation()
        print("🎉 All group-schedules contract tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
