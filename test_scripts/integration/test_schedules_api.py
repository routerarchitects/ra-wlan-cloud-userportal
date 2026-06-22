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

CREATED_SCHEDULE_ID = None

def test_post_schedules():
    global CREATED_SCHEDULE_ID
    print("Testing POST /schedules stateful create...")
    payload = {
        "name": "test-schedule",
        "description": "desc",
        "action_type": "BLOCK",
        "target_kind": "INTERNET",
        "target_value": None,
        "start_time": "08:00",
        "stop_time": "17:00",
        "weekdays": [1, 2, 3]
    }
    status, body = request("POST", "/api/v1/schedules", body=payload)
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, dict), f"Expected JSON object, got {type(body)}. Body: {body}"
    assert "id" in body and "name" in body, f"Expected created schedule fields. Body: {body}"
    assert body["name"] == "test-schedule"
    assert "start_time" in body and body["start_time"] == "08:00"
    assert "stop_time" in body and body["stop_time"] == "17:00"
    assert "start_minute" not in body
    assert "stop_minute" not in body
    CREATED_SCHEDULE_ID = body["id"]
    print(f"✅ POST /schedules passed, created ID: {CREATED_SCHEDULE_ID}")

def test_get_schedules():
    print("Testing GET /schedules stateful list...")
    status, body = request("GET", "/api/v1/schedules")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, list), f"Expected JSON array, got {type(body)}. Body: {body}"
    assert any(s.get("id") == CREATED_SCHEDULE_ID for s in body), f"Expected newly created schedule in list. Body: {body}"
    
    # Assert normalization on list
    for s in body:
        assert "start_minute" not in s, f"start_minute should be absent from list item, got {s}"
        assert "stop_minute" not in s, f"stop_minute should be absent from list item, got {s}"
        assert "start_time" in s, f"start_time should be present in list item, got {s}"
        assert "stop_time" in s, f"stop_time should be present in list item, got {s}"
    print("✅ GET /schedules passed")

def test_get_schedule_by_id():
    print("Testing GET /schedules/{id} stateful read...")
    status, body = request("GET", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, dict), f"Expected JSON object. Body: {body}"
    assert body.get("id") == CREATED_SCHEDULE_ID, f"Expected matching ID. Body: {body}"
    assert body.get("name") == "test-schedule"
    
    # Assert normalization on read
    assert "start_minute" not in body, "start_minute should be absent from read"
    assert "stop_minute" not in body, "stop_minute should be absent from read"
    assert body.get("start_time") == "08:00", f"Expected normalized start_time '08:00', got {body.get('start_time')}"
    assert body.get("stop_time") == "17:00", f"Expected normalized stop_time '17:00', got {body.get('stop_time')}"
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
        "start_time": "09:00",
        "stop_time": "18:00",
        "weekdays": [0, 6]
    }
    status, body = request("PUT", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}", body=payload)
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert body.get("name") == "updated-schedule", f"Expected updated name. Body: {body}"
    assert body.get("enabled") is False, f"Expected updated enabled. Body: {body}"
    assert body.get("target_kind") == "APP"
    assert body.get("target_value") == "YouTube"
    assert body.get("start_time") == "09:00"
    
    # Verify update persisted
    status, read_body = request("GET", f"/api/v1/schedules/{CREATED_SCHEDULE_ID}")
    assert read_body.get("name") == "updated-schedule", "GET after PUT did not return updated name"
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
    status, body = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="config-raw")
    assert status == 200, f"Expected 200, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert any("inventory" in call["path"] or "subscriber" in call["path"] for call in obs["calls"]), "Provisioning lookup not called on PUT"
        assert any("device" in call["path"] and call["method"] == "GET" for call in obs["calls"]), "Gateway get-config not called on PUT"
        assert any("configure" in call["path"] and call["method"] == "POST" for call in obs["calls"]), "Gateway configure not called on PUT"
        
        payload_gw = obs.get("last_configure_payload")
        assert payload_gw is not None, "Gateway configure payload not recorded"
        assert "configuration" in payload_gw, "Payload missing 'configuration'"
        assert "config-raw" in payload_gw["configuration"], "config-raw missing from configuration"
        
        config_raw = payload_gw["configuration"]["config-raw"]
        assert any(len(entry) > 1 and entry[1] == "parental_control.ci_rule.enabled" for entry in config_raw), "Missing parental_control.ci_rule.enabled from replaced config-raw"
        assert not any(len(entry) > 1 and entry[1] == "wifi.ssid" for entry in config_raw), "wifi.ssid was preserved but replacement-only contract requires direct replacement"
        print("✅ PUT config-raw happy path passed")

    # Failure-path PUT with scenario "delete-config-raw-prov-502"
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="delete-config-raw-prov-502")
    assert status == 500, f"Expected 500 for provisioning failure on PUT, got {status}"
    
    # Failure-path PUT with scenario "delete-config-raw-gw-get-malformed"
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="delete-config-raw-gw-get-malformed")
    assert status == 500, f"Expected 500 for gw-get malformed failure on PUT, got {status}"

    # Failure-path PUT with scenario "delete-config-raw-gw-get-502"
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw get failure on PUT, got {status}"
    
    # Failure-path PUT with scenario "delete-config-raw-gw-configure-502"
    status, _ = request("PUT", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", body=payload, scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure on PUT, got {status}"
    
    print("✅ PUT config-raw error scenarios passed")

def test_delete_orchestration():
    print("Testing DELETE /schedules/{id} config-raw orchestrations...")
    status, body = request("DELETE", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="config-raw")
    assert status == 200, f"Expected 200, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert any("inventory" in call["path"] or "subscriber" in call["path"] for call in obs["calls"]), "Provisioning lookup not called"
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
    status, _ = request("DELETE", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="delete-config-raw-prov-502")
    assert status == 500, f"Expected 500 for provisioning failure, got {status}"
    
    # DELETE item with scenario "delete-config-raw-gw-get-malformed"
    status, _ = request("DELETE", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="delete-config-raw-gw-get-malformed")
    assert status == 500, f"Expected 500 for gw-get malformed failure, got {status}"

    # DELETE item with scenario "delete-config-raw-gw-get-502"
    status, _ = request("DELETE", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw get failure, got {status}"
    
    # DELETE item with scenario "delete-config-raw-gw-configure-502"
    status, _ = request("DELETE", f"/api/v1/schedules/{VALID_SCHEDULE_ID}", scenario="delete-config-raw-gw-configure-502")
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
        assert not any("inventory" in call["path"] or "subscriber" in call["path"] for call in obs["calls"]), "Provisioning called unexpectedly on config-raw-null PUT"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway GET called unexpectedly on config-raw-null PUT"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure called unexpectedly on config-raw-null PUT"

    # DELETE path under config-raw-null scenario: must succeed and not return 500 or trigger gw update
    status, body = request("DELETE", f"/api/v1/schedules/{tid}", scenario="config-raw-null")
    assert status == 200, f"Expected 200 for DELETE with config-raw: null, got {status}"
    
    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        assert not any("inventory" in call["path"] or "subscriber" in call["path"] for call in obs["calls"]), "Provisioning called unexpectedly on config-raw-null DELETE"
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
        assert not any("inventory" in call["path"] or "subscriber" in call["path"] for call in obs["calls"]), "Provisioning lookup unexpectedly called on POST"
        assert not any("device" in call["path"] for call in obs["calls"]), "Gateway get-config unexpectedly called on POST"
        assert not any("configure" in call["path"] for call in obs["calls"]), "Gateway configure unexpectedly called on POST"
        
    print("✅ POST config-raw skip-apply passed")

if __name__ == "__main__":
    print("Starting schedules integration tests...")
    try:
        reset_db()
        test_post_schedules()
        test_get_schedules()
        test_get_schedule_by_id()
        test_put_schedules()
        test_delete_schedules_normal()

        test_forwarded_failures()
        test_put_orchestration()
        test_delete_orchestration()
        test_config_raw_null_handling()
        test_post_config_raw_skip_apply()
        
        print("🎉 All schedules integration tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
