import urllib.request
import urllib.error
import json
import os
import sys
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
VALID_SCHEDULE_ID = "22222222-2222-4222-8222-222222222222"

HTTPS_CONTEXT = ssl._create_unverified_context()

def open_url(req_or_url):
    url = req_or_url.full_url if hasattr(req_or_url, "full_url") else req_or_url
    if str(url).startswith("https://"):
        return urllib.request.urlopen(req_or_url, context=HTTPS_CONTEXT)
    return urllib.request.urlopen(req_or_url)

def reset_observations():
    req = urllib.request.Request(f"{FAKE_URL}/reset-observations", data=b"", method="POST")
    open_url(req)
    req2 = urllib.request.Request(
        f"{FAKE_URL}/set-scenario",
        data=json.dumps({"scenario": "normal"}).encode(),
        method="POST"
    )
    open_url(req2)

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
        assert len(obs["calls"]) == 0, f"{test_name}: Downstream services were unexpectedly called: {obs['calls']}"

def check_downstream_called(test_name):
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert len(obs["calls"]) > 0, f"{test_name}: Request was not forwarded downstream (no calls observed)"

def get_observations():
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        return obs["calls"]

def test_auth_checks():
    print("Testing Auth Rejections...")
    reset_observations()
    status, _ = request("GET", "/api/v1/schedules", headers={})
    assert status == 403, f"Expected 403 for missing auth, got {status}"
    check_no_observations("GET /schedules missing auth")

    reset_observations()
    status, _ = request("GET", "/api/v1/schedules", headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth, got {status}"
    check_no_observations("GET /schedules bad auth")
    print("✅ Auth tests passed")

def test_local_validation():
    print("Testing Local Request Validation...")

    # GET invalid UUID
    reset_observations()
    status, _ = request("GET", "/api/v1/schedules/12345")
    assert status == 400, f"Expected 400 for invalid UUID, got {status}"
    check_no_observations("GET invalid UUID")
    
    # POST unknown field
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1],
        "extra": "bad"
    })
    assert status == 400, f"Expected 400 for unknown field, got {status}"
    check_no_observations("POST unknown field")
    
    # POST missing name
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for missing name, got {status}"
    check_no_observations("POST missing name")
    
    # POST empty name
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "   ", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for empty name, got {status}"
    check_no_observations("POST empty name")

    # POST invalid description type
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": 123, "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for invalid description type, got {status}"
    check_no_observations("POST invalid description type")

    # POST invalid action_type
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "ALLOW",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for invalid action_type, got {status}"
    check_no_observations("POST invalid action_type")

    # POST missing action_type
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for missing action_type, got {status}"
    check_no_observations("POST missing action_type")

    # POST invalid target_kind
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "WEBSITE", "target_value": "test.com",
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for invalid target_kind, got {status}"
    check_no_observations("POST invalid target_kind")

    # POST missing target_kind
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for missing target_kind, got {status}"
    check_no_observations("POST missing target_kind")

    # POST APP target without target_value (missing key entirely) — must reject
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "APP",
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for APP schedule with missing target_value, got {status}"
    check_no_observations("POST APP target with missing target_value")

    # POST APP target with null target_value — must reject
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "APP", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for APP schedule with null target_value, got {status}"
    check_no_observations("POST APP target with null target_value")

    # POST APP target with empty-string target_value
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "APP", "target_value": "",
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for APP schedule with empty target_value, got {status}"
    check_no_observations("POST APP target with empty target_value")

    # POST INTERNET target with non-null target_value — must reject
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": "something",
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for INTERNET schedule with non-null target_value, got {status}"
    check_no_observations("POST INTERNET target with non-null target_value")

    # POST INTERNET target with target_value omitted entirely — must be forwarded downstream, not rejected locally.
    # The fake server creates the schedule and returns 200, proving local validation passed.
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 200, f"Expected 200 for INTERNET schedule without target_value key, got {status}"
    check_downstream_called("POST INTERNET without target_value (forwarded)")

    # POST invalid start_time format
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "8:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for invalid start_time format, got {status}"
    check_no_observations("POST invalid start_time format")

    # POST invalid stop_time format
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:0", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for invalid stop_time format, got {status}"
    check_no_observations("POST invalid stop_time format")

    # POST equal start_time and stop_time
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "08:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for equal start and stop times, got {status}"
    check_no_observations("POST equal start_time and stop_time")

    # POST empty weekdays
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": []
    })
    assert status == 400, f"Expected 400 for empty weekdays, got {status}"
    check_no_observations("POST empty weekdays")

    # POST missing weekdays
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00"
    })
    assert status == 400, f"Expected 400 for missing weekdays, got {status}"
    check_no_observations("POST missing weekdays")

    # POST invalid weekdays (out of range)
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [7]
    })
    assert status == 400, f"Expected 400 for invalid weekday value, got {status}"
    check_no_observations("POST invalid weekday value")

    # POST invalid weekdays (duplicates)
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1, 1]
    })
    assert status == 400, f"Expected 400 for duplicate weekdays, got {status}"
    check_no_observations("POST duplicate weekdays")

    # POST malformed JSON
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/schedules", data=b"{bad-json", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="POST")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for malformed POST json, got {status}"
    check_no_observations("POST malformed json")

    # PUT with description omitted — must be forwarded downstream, not rejected locally.
    # VALID_SCHEDULE_ID does not exist in the fake DB, so downstream returns 404.
    # A 404 from downstream is proof the request passed local validation and was forwarded.
    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body={
        "name": "test", "enabled": True, "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 404, f"Expected 404 (forwarded to downstream, schedule not found), got {status}"
    check_downstream_called("PUT without description (forwarded)")

    # PUT with invalid description type — must still reject
    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body={
        "name": "test", "description": 999, "enabled": True, "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for invalid description type on PUT, got {status}"
    check_no_observations("PUT invalid description type")

    # PUT missing enabled
    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body={
        "name": "test", "description": "desc", "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1]
    })
    assert status == 400, f"Expected 400 for missing enabled on PUT, got {status}"
    check_no_observations("PUT missing enabled")
    
    # PUT unknown field
    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body={
        "name": "test", "description": "desc", "enabled": True, "action_type": "BLOCK",
        "target_kind": "INTERNET", "target_value": None,
        "start_time": "08:00", "stop_time": "17:00", "weekdays": [1],
        "extra": "bad"
    })
    assert status == 400, f"Expected 400 for unknown field on PUT, got {status}"
    check_no_observations("PUT unknown field")

    # PUT malformed JSON
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/schedules/{VALID_SCHEDULE_ID}", data=b"{bad-json", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="PUT")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for malformed PUT json, got {status}"
    check_no_observations("PUT malformed json")

    # POST missing body
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/schedules", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="POST")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for POST missing body, got {status}"
    check_no_observations("POST missing body")

    # PUT missing body
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/schedules/{VALID_SCHEDULE_ID}", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="PUT")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for PUT missing body, got {status}"
    check_no_observations("PUT missing body")

    print("✅ Local validation tests passed")

def test_forwarded_payloads():
    print("Testing Schedule Forwarded Downstream Payloads...")

    # Case 1: POST /api/v1/schedules with description and target_value omitted
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "Omitted fields test",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    })
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    assert len(calls) > 0, "No calls recorded downstream"
    schedule_calls = [c for c in calls if c["method"] == "POST" and "/schedules" in c["path"]]
    assert len(schedule_calls) > 0, f"No schedule creation call found in: {calls}"
    body = schedule_calls[0].get("body", {})
    assert "description" not in body, f"description should be omitted from downstream payload, but was: {body}"
    assert "target_value" not in body, f"target_value should be omitted from downstream payload, but was: {body}"

    # Case 2: POST /api/v1/schedules with description: null and target_value: null explicitly
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "Explicit null fields test",
        "description": None,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    })
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    schedule_calls = [c for c in calls if c["method"] == "POST" and "/schedules" in c["path"]]
    assert len(schedule_calls) > 0
    body = schedule_calls[0].get("body", {})
    assert "description" in body and body["description"] is None, f"description should be explicitly null in downstream payload, but was: {body}"
    assert "target_value" in body and body["target_value"] is None, f"target_value should be explicitly null in downstream payload, but was: {body}"

    # Case 3: POST /api/v1/schedules with description as string
    reset_observations()
    status, _ = request("POST", "/api/v1/schedules", body={
        "name": "String desc test",
        "description": "My test schedule",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    })
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    schedule_calls = [c for c in calls if c["method"] == "POST" and "/schedules" in c["path"]]
    assert len(schedule_calls) > 0
    body = schedule_calls[0].get("body", {})
    assert body.get("description") == "My test schedule", f"description should be 'My test schedule', but was: {body}"
    assert "target_value" not in body, f"target_value should be omitted, but was: {body}"

    # Case 4: PUT /api/v1/schedules/{id} with description and target_value omitted
    reset_observations()
    status, res = request("POST", "/api/v1/schedules", body={
        "name": "For PUT test",
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    })
    assert status == 200, f"Expected 200, got {status}"
    sched_id = res.get("id")
    assert sched_id is not None, f"POST response should contain id: {res}"

    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body={
        "name": "Updated name",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "09:00",
        "stop_time": "18:00",
        "weekdays": [2]
    })
    assert status == 200, f"Expected 200 for PUT, got {status}"
    calls = get_observations()
    put_calls = [c for c in calls if c["method"] == "PUT" and f"/schedules/{sched_id}" in c["path"]]
    assert len(put_calls) > 0, f"No PUT call found: {calls}"
    body = put_calls[0].get("body", {})
    assert "description" not in body, f"description should be omitted from downstream PUT payload, but was: {body}"
    assert "target_value" not in body, f"target_value should be omitted from downstream PUT payload, but was: {body}"

    # Case 5: PUT /api/v1/schedules/{id} with description: null and target_value: null explicitly
    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body={
        "name": "Updated name",
        "description": None,
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "09:00",
        "stop_time": "18:00",
        "weekdays": [2]
    })
    assert status == 200, f"Expected 200 for PUT, got {status}"
    calls = get_observations()
    put_calls = [c for c in calls if c["method"] == "PUT" and f"/schedules/{sched_id}" in c["path"]]
    assert len(put_calls) > 0
    body = put_calls[0].get("body", {})
    assert "description" in body and body["description"] is None, f"description should be null in downstream PUT payload, but was: {body}"
    assert "target_value" in body and body["target_value"] is None, f"target_value should be null in downstream PUT payload, but was: {body}"

    # Case 6: PUT /api/v1/schedules/{id} with description string and target_value omitted
    reset_observations()
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body={
        "name": "Updated name",
        "description": "Updated string description",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "09:00",
        "stop_time": "18:00",
        "weekdays": [2]
    })
    assert status == 200, f"Expected 200 for PUT, got {status}"
    calls = get_observations()
    put_calls = [c for c in calls if c["method"] == "PUT" and f"/schedules/{sched_id}" in c["path"]]
    assert len(put_calls) > 0
    body = put_calls[0].get("body", {})
    assert body.get("description") == "Updated string description", f"description should be updated description, but was: {body}"
    assert "target_value" not in body, f"target_value should be omitted in downstream PUT payload, but was: {body}"

    print("✅ Schedule forwarded payload tests passed")

if __name__ == "__main__":
    print("Starting schedules contract tests...")
    try:
        test_auth_checks()
        test_local_validation()
        test_forwarded_payloads()
        print("🎉 All contract tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
