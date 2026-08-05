import urllib.request
import urllib.error
import json
import os
import sys
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
TOKEN = "dummy-test-token"
VALID_SCHEDULE_ID = "22222222-2222-4222-8222-222222222222"

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
        if isinstance(body, dict) or isinstance(body, list):
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
        except:
            return e.code, res_body

def get_pc_calls():
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        return [call for call in obs.get("calls", []) if call["path"].startswith("/api/v1/subscribers/")]

def assert_schedule_fields(sched, expected_id=None, expected_name=None, expected_start_time=None, expected_stop_time=None, expected_weekdays=None):
    assert isinstance(sched, dict), f"Expected dict for Schedule, got {type(sched)}"
    for field in ["id", "subscriber_id", "schedule_config_index", "name", "description", "enabled", "action_type", "target_kind", "target_value", "start_time", "stop_time", "weekdays", "created_at", "updated_at"]:
        assert field in sched, f"Expected field '{field}' in Schedule: {sched}"
    assert "start_minute" not in sched, f"Forbidden field 'start_minute' in Schedule: {sched}"
    assert "stop_minute" not in sched, f"Forbidden field 'stop_minute' in Schedule: {sched}"
    assert "config-raw" not in sched, f"Forbidden field 'config-raw' in Schedule: {sched}"
    if expected_id is not None:
        assert sched["id"] == expected_id, f"Expected id {expected_id}, got {sched['id']}"
    if expected_name is not None:
        assert sched["name"] == expected_name, f"Expected name {expected_name}, got {sched['name']}"
    if expected_start_time is not None:
        assert sched["start_time"] == expected_start_time, f"Expected start_time {expected_start_time}, got {sched['start_time']}"
    if expected_stop_time is not None:
        assert sched["stop_time"] == expected_stop_time, f"Expected stop_time {expected_stop_time}, got {sched['stop_time']}"
    if expected_weekdays is not None:
        assert sched["weekdays"] == expected_weekdays, f"Expected weekdays {expected_weekdays}, got {sched['weekdays']}"

CREATED_SCHEDULE_ID = None

def test_timezone_resolution_failures():
    print("Testing timezone resolution failure scenarios before schedule calls...")

    # A. GET schedule list with no stored schedules
    reset_db()
    for sc, exp_code in [("timezone-missing", 400), ("timezone-invalid", 500)]:
        status, body = request("GET", "/api/v1/schedules", scenario=sc)
        assert status == exp_code, f"GET list (empty DB) under {sc}: expected {exp_code}, got {status}. Body: {body}"
        pc_calls = get_pc_calls()
        assert len(pc_calls) == 0, f"Expected 0 parental control calls under {sc}, got {pc_calls}"

    # B. GET schedule list with an existing schedule
    reset_db()
    sched_id = create_test_schedule("tz-test-schedule")
    for sc, exp_code in [("timezone-missing", 400), ("timezone-invalid", 500)]:
        status, body = request("GET", "/api/v1/schedules", scenario=sc)
        assert status == exp_code, f"GET list (with schedule) under {sc}: expected {exp_code}, got {status}. Body: {body}"
        pc_calls = get_pc_calls()
        assert len(pc_calls) == 0, f"Expected 0 parental control calls under {sc}, got {pc_calls}"

    # C. GET one schedule
    for sc, exp_code in [("timezone-missing", 400), ("timezone-invalid", 500)]:
        status, body = request("GET", f"/api/v1/schedules/{sched_id}", scenario=sc)
        assert status == exp_code, f"GET one schedule under {sc}: expected {exp_code}, got {status}. Body: {body}"
        pc_calls = get_pc_calls()
        assert len(pc_calls) == 0, f"Expected 0 parental control calls under {sc}, got {pc_calls}"

    # D. POST schedule
    post_payload = {
        "name": "tz-post-schedule",
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "00:30",
        "stop_time": "06:15",
        "weekdays": [1, 3]
    }
    for sc, exp_code in [("timezone-missing", 400), ("timezone-invalid", 500)]:
        status, body = request("POST", "/api/v1/schedules", body=post_payload, scenario=sc)
        assert status == exp_code, f"POST schedule under {sc}: expected {exp_code}, got {status}. Body: {body}"
        pc_calls = get_pc_calls()
        assert len(pc_calls) == 0, f"Expected 0 parental control calls under {sc}, got {pc_calls}"

    # E. PUT schedule
    put_payload = {
        "name": "tz-put-schedule",
        "description": "new-desc",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "00:30",
        "stop_time": "06:15",
        "weekdays": [1, 3]
    }
    for sc, exp_code in [("timezone-missing", 400), ("timezone-invalid", 500)]:
        status, body = request("PUT", f"/api/v1/schedules/{sched_id}", body=put_payload, scenario=sc)
        assert status == exp_code, f"PUT schedule under {sc}: expected {exp_code}, got {status}. Body: {body}"
        pc_calls = get_pc_calls()
        assert len(pc_calls) == 0, f"Expected 0 parental control calls under {sc}, got {pc_calls}"

    reset_db()
    set_scenario("normal")
    print("✅ Timezone resolution failure tests passed")

def test_post_schedules():
    global CREATED_SCHEDULE_ID
    print("Testing POST /schedules stateful create...")
    payload = {
        "name": "test-schedule",
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "00:30",
        "stop_time": "06:15",
        "weekdays": [1, 3]
    }
    status, body = request("POST", "/api/v1/schedules", body=payload)
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert_schedule_fields(body, None, "test-schedule", "00:30", "06:15", [1, 3])
    CREATED_SCHEDULE_ID = body["id"]
    print(f"✅ POST /schedules passed, created ID: {CREATED_SCHEDULE_ID}")

def test_post_schedules_internet_no_target_value():
    """INTERNET schedules with target_value omitted entirely must be accepted."""
    print("Testing POST /schedules INTERNET without target_value key...")
    payload = {
        "name": "internet-no-tv",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "start_time": "10:00",
        "stop_time": "20:00",
        "weekdays": [0]
    }
    status, body = request("POST", "/api/v1/schedules", body=payload)
    assert status == 200, f"Expected 200 for INTERNET schedule without target_value, got {status}. Body: {body}"
    assert_schedule_fields(body, None, "internet-no-tv", "10:00", "20:00", [0])
    # Clean up immediately so this extra record does not affect subsequent stateful assertions.
    del_status, _ = request("DELETE", f"/api/v1/schedules/{body['id']}")
    assert del_status == 200, f"Expected 200 on cleanup delete, got {del_status}"
    print("✅ POST INTERNET without target_value passed")

def test_get_schedules():
    print("Testing GET /schedules stateful list...")
    status, body = request("GET", "/api/v1/schedules")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, list), f"Expected JSON array, got {type(body)}. Body: {body}"
    assert any(s.get("id") == CREATED_SCHEDULE_ID for s in body), f"Expected newly created schedule in list. Body: {body}"
    
    # Assert normalization on list
    for s in body:
        assert_schedule_fields(s)
        if s.get("id") == CREATED_SCHEDULE_ID:
            assert s["start_time"] == "00:30"
            assert s["stop_time"] == "06:15"
            assert s["weekdays"] == [1, 3]
    print("✅ GET /schedules passed")

def test_get_schedule_by_id():
    print("Testing GET /schedules/{id} stateful read...")
    status, body = request("GET", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert_schedule_fields(body, CREATED_SCHEDULE_ID, "test-schedule", "00:30", "06:15", [1, 3])
    print("✅ GET /schedules/{id} passed")

def test_put_schedules():
    print("Testing PUT /schedules/{id} stateful update...")
    payload = {
        "name": "updated-schedule",
        "description": "new-desc",
        "enabled": False,
        "action_type": "BLOCK",
        "target_kind": "APP",
        "target_value": "YouTube",
        "start_time": "00:30",
        "stop_time": "06:15",
        "weekdays": [1, 3]
    }
    status, body = request("PUT", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}", body=payload)
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert_schedule_fields(body, CREATED_SCHEDULE_ID, "updated-schedule", "00:30", "06:15", [1, 3])
    assert body.get("enabled") is False
    assert body.get("target_kind") == "APP"
    assert body.get("target_value") == "YouTube"
    
    # Verify update persisted
    status, read_body = request("GET", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}")
    assert_schedule_fields(read_body, CREATED_SCHEDULE_ID, "updated-schedule", "00:30", "06:15", [1, 3])

    # PUT with description omitted (description is optional on PUT) — must also succeed
    payload_no_desc = {
        "name": "no-desc-update",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "00:30",
        "stop_time": "06:15",
        "weekdays": [1, 3]
    }
    status, body = request("PUT", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}", body=payload_no_desc)
    assert status == 200, f"Expected 200 for PUT without description, got {status}. Body: {body}"
    assert_schedule_fields(body, CREATED_SCHEDULE_ID, "no-desc-update", "00:30", "06:15", [1, 3])
    print("✅ PUT /schedules passed")

def test_delete_schedules_normal():
    print("Testing DELETE /schedules/{id} stateful delete...")
    status, body = request("DELETE", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    
    # Verify deletion persisted
    status, _ = request("GET", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}")
    assert status == 404, f"Expected 404 after deletion, got {status}"
    print("✅ DELETE /schedules normal passed")

def test_forwarded_failures():
    print("Testing forwarded downstream failures...")
    status, _ = request("GET", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="pc-404")
    assert status == 404, f"Expected 404, got {status}"
    
    payload = {
        "name": "test",
        "description": "desc",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    }
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="pc-409")
    assert status == 409, f"Expected 409, got {status}"

    status, _ = request("POST", "/api/v1/schedules", body=payload, scenario="pc-409")
    assert status == 409, f"Expected 409 for POST conflict, got {status}"

    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="pc-404")
    assert status == 404, f"Expected 404 for PUT not-found, got {status}"

    status, _ = request("DELETE", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="pc-404")
    assert status == 404, f"Expected 404 for DELETE not-found, got {status}"

    print("✅ Forwarded downstream failure tests passed")

def create_test_schedule(name="test"):
    payload = {
        "name": name,
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    }
    status, body = request("POST", "/api/v1/schedules", body=payload)
    assert status == 200, f"Expected 200 on test schedule creation, got {status}"
    return body["id"]

def test_put_orchestration():
    print("Testing PUT /schedules/{id} config-raw orchestrations...")
    payload = {
        "name": "orch-schedule",
        "description": "desc",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    }
    # Happy path config-raw
    sched_id = create_test_schedule("orch-happy-path")
    status, body = request("PUT", f"/api/v1/schedules/{sched_id}", body=payload, scenario="config-raw")
    assert status == 200, f"Expected 200, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning lookup not called on PUT"
        assert any("device" in call["path"] and call["method"] == "GET" for call in obs["calls"]), "Gateway get-config not called on PUT"
        assert any("configure" in call["path"] and call["method"] == "POST" for call in obs["calls"]), "Gateway configure not called on PUT"
        
        payload_gw = obs.get("last_configure_payload")
        assert payload_gw is not None, "Gateway configure payload not recorded"
        assert "configuration" in payload_gw, "Payload missing 'configuration'"
        assert "config-raw" in payload_gw["configuration"], "config-raw missing from configuration"
        
        config_raw = payload_gw["configuration"]["config-raw"]
        assert any(len(entry) > 1 and entry[1] == "parental_control.ci_rule.enabled" for entry in config_raw), "Missing parental_control.ci_rule.enabled from replaced config-raw"
        # This test intentionally verifies full replacement of gateway config-raw with the
        # downstream parental-control snapshot; unrelated older entries like wifi.ssid are
        # expected to be removed under the current system design.
        assert not any(len(entry) > 1 and entry[1] == "wifi.ssid" for entry in config_raw), "wifi.ssid was preserved but replacement-only contract requires direct replacement"
        print("✅ PUT config-raw happy path passed")

    # Failure-path PUT with scenario "delete-config-raw-prov-502"
    sched_id = create_test_schedule("orch-prov-502")
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body=payload, scenario="delete-config-raw-prov-502")
    assert status == 500, f"Expected 500 for provisioning failure on PUT, got {status}"
    
    # Failure-path PUT with scenario "delete-config-raw-gw-get-malformed"
    sched_id = create_test_schedule("orch-gw-malformed")
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body=payload, scenario="delete-config-raw-gw-get-malformed")
    assert status == 500, f"Expected 500 for gw-get malformed failure on PUT, got {status}"

    # Failure-path PUT with scenario "delete-config-raw-gw-get-502"
    sched_id = create_test_schedule("orch-gw-get-502")
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body=payload, scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw get failure on PUT, got {status}"
    
    # Failure-path PUT with scenario "delete-config-raw-gw-configure-502"
    sched_id = create_test_schedule("orch-gw-conf-502")
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body=payload, scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure on PUT, got {status}"
    
    print("✅ PUT config-raw error scenarios passed")

def test_delete_orchestration():
    print("Testing DELETE /schedules/{id} config-raw orchestrations...")
    # Happy path config-raw
    sched_id = create_test_schedule("del-happy-path")
    status, body = request("DELETE", f"/api/v1/schedules/{sched_id}", scenario="config-raw")
    assert status == 200, f"Expected 200, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning lookup not called"
        assert any("device" in call["path"] and call["method"] == "GET" for call in obs["calls"]), "Gateway get-config not called"
        assert any("configure" in call["path"] and call["method"] == "POST" for call in obs["calls"]), "Gateway configure not called"
        
        payload = obs.get("last_configure_payload")
        assert payload is not None, "Gateway configure payload not recorded"
        assert "configuration" in payload, "Payload missing 'configuration'"
        assert "config-raw" in payload["configuration"], "config-raw missing from configuration"
        
        config_raw = payload["configuration"]["config-raw"]
        assert not any(len(entry) > 1 and entry[1] == "wifi.ssid" for entry in config_raw), "wifi.ssid was preserved but replacement-only contract requires direct replacement"
        assert any(len(entry) > 1 and entry[1] == "parental_control.ci_rule.enabled" for entry in config_raw), "Missing parental_control.ci_rule.enabled from replaced config-raw"
        print("✅ DELETE config-raw happy path passed")

    # DELETE item with scenario "delete-config-raw-prov-502"
    sched_id = create_test_schedule("del-prov-502")
    status, _ = request("DELETE", f"/api/v1/schedules/{sched_id}", scenario="delete-config-raw-prov-502")
    assert status == 500, f"Expected 500 for provisioning failure, got {status}"
    
    # DELETE item with scenario "delete-config-raw-gw-get-malformed"
    sched_id = create_test_schedule("del-gw-malformed")
    status, _ = request("DELETE", f"/api/v1/schedules/{sched_id}", scenario="delete-config-raw-gw-get-malformed")
    assert status == 500, f"Expected 500 for gw-get malformed failure, got {status}"

    # DELETE item with scenario "delete-config-raw-gw-get-502"
    sched_id = create_test_schedule("del-gw-get-502")
    status, _ = request("DELETE", f"/api/v1/schedules/{sched_id}", scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw get failure, got {status}"
    
    # DELETE item with scenario "delete-config-raw-gw-configure-502"
    sched_id = create_test_schedule("del-gw-conf-502")
    status, _ = request("DELETE", f"/api/v1/schedules/{sched_id}", scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure, got {status}"
    
    print("✅ DELETE config-raw error scenarios passed")

def test_config_raw_null_handling():
    print("Testing config-raw: null handling...")
    
    # Ensure POST doesn't fail
    payload = {
        "name": "null-config-raw-test",
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    }
    status, body = request("POST", "/api/v1/schedules", body=payload)
    assert status == 200, f"Expected 200, got {status}"
    tid = body["id"]

    # PUT path under config-raw-null scenario: must succeed and not return 500 or trigger gw update
    payload["enabled"] = False
    status, body = request("PUT", f"/api/v1/schedules/{tid}", body=payload, scenario="config-raw-null")
    assert status == 200, f"Expected 200 for PUT with config-raw: null, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert not any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning called unexpectedly on config-raw-null PUT"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway GET called unexpectedly on config-raw-null PUT"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure called unexpectedly on config-raw-null PUT"

    # DELETE path under config-raw-null scenario: must succeed and not return 500 or trigger gw update
    status, body = request("DELETE", f"/api/v1/schedules/{tid}", scenario="config-raw-null")
    assert status == 200, f"Expected 200 for DELETE with config-raw: null, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert not any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning called unexpectedly on config-raw-null DELETE"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway GET called unexpectedly on config-raw-null DELETE"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure called unexpectedly on config-raw-null DELETE"

    print("✅ config-raw: null handling tests passed")

def test_post_config_raw_skip_apply():
    print("Testing POST /schedules with scenario config-raw (skip-apply)...")
    payload = {
        "name": "post-skip-apply-schedule",
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    }
    # When scenario is config-raw, POST should succeed but not perform apply
    status, body = request("POST", "/api/v1/schedules", body=payload, scenario="config-raw")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert "id" in body
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert any("schedules" in call["path"] and call["method"] == "POST" for call in obs["calls"]), "Downstream schedules POST not called"
        assert not any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning lookup unexpectedly called on POST"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway get-config unexpectedly called on POST"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure unexpectedly called on POST"
        
    print("✅ POST config-raw skip-apply passed")

def test_config_raw_malformed_handling():
    print("Testing config-raw: malformed handling...")
    
    # 1. Test PUT with malformed config-raw
    sched_id = create_test_schedule("malformed-put-test")
    payload = {
        "name": "malformed-put-test",
        "description": "desc",
        "enabled": True,
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1]
    }
    status, _ = request("PUT", f"/api/v1/schedules/{sched_id}", body=payload, scenario="config-raw-malformed")
    assert status == 500, f"Expected 500 for malformed config-raw PUT, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert not any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning lookup unexpectedly called on malformed PUT"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway get-config unexpectedly called on malformed PUT"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure called unexpectedly on malformed PUT"

    # 2. Test DELETE with malformed config-raw
    sched_id = create_test_schedule("malformed-del-test")
    status, _ = request("DELETE", f"/api/v1/schedules/{sched_id}", scenario="config-raw-malformed")
    assert status == 500, f"Expected 500 for malformed config-raw DELETE, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert not any("inventory" in call["path"] or "subscriberDevice" in call["path"] for call in obs["calls"]), "Provisioning lookup unexpectedly called on malformed DELETE"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway get-config unexpectedly called on malformed DELETE"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure called unexpectedly on malformed DELETE"

    print("✅ config-raw: malformed handling tests passed")

if __name__ == "__main__":
    print("Starting schedules integration tests...")
    try:
        reset_db()
        test_timezone_resolution_failures()
        test_post_schedules()
        test_post_schedules_internet_no_target_value()
        test_get_schedules()
        test_get_schedule_by_id()
        test_put_schedules()
        test_delete_schedules_normal()

        test_forwarded_failures()
        test_put_orchestration()
        test_delete_orchestration()
        test_config_raw_null_handling()
        test_post_config_raw_skip_apply()
        test_config_raw_malformed_handling()
        
        print("🎉 All schedules integration tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
