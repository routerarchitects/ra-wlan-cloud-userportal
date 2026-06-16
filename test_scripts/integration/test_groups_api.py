import urllib.request
import urllib.error
import json
import os
import sys
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
TOKEN = "dummy-test-token"
VALID_GROUP_ID = "11111111-1111-4111-8111-111111111111"

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

def test_get_groups():
    print("Testing GET /groups happy path...")
    status, body = request("GET", "/api/v1/groups")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, list), f"Expected JSON array, got {type(body)}. Body: {body}"
    assert len(body) > 0, f"Expected non-empty array. Body: {body}"
    assert "id" in body[0] and "name" in body[0], f"Expected id and name in group. Body: {body}"
    print("✅ GET /groups passed")

def test_post_groups():
    print("Testing POST /groups happy path...")
    status, body = request("POST", "/api/v1/groups", body={"name": "test", "description": "desc"})
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, dict), f"Expected JSON object, got {type(body)}. Body: {body}"
    assert "id" in body and "name" in body, f"Expected created group fields. Body: {body}"
    print("✅ POST /groups passed")

def test_get_group_by_id():
    print("Testing GET /groups/{id} happy path...")
    status, body = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, dict), f"Expected JSON object. Body: {body}"
    assert "id" in body and "name" in body, f"Expected group fields. Body: {body}"
    print("✅ GET /groups/{id} passed")

def test_put_groups():
    print("Testing PUT /groups/{id} happy path...")
    status, body = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}", body={"name": "test", "description": "desc"})
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, dict), f"Expected JSON object. Body: {body}"
    assert "id" in body and "name" in body, f"Expected updated group fields. Body: {body}"
    print("✅ PUT /groups passed")

def test_delete_groups_normal():
    print("Testing DELETE /groups/{id} normal success without config-raw...")
    status, body = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert body == {}, f"Expected empty body for success. Body: {body}"
    print("✅ DELETE /groups normal passed")

def test_forwarded_failures():
    print("Testing forwarded downstream failures...")
    # H) GET item with scenario "pc-404"
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}", scenario="pc-404")
    assert status == 404, f"Expected 404, got {status}"
    
    # I) PUT item with scenario "pc-409"
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}", body={"name":"test","description":"desc"}, scenario="pc-409")
    assert status == 409, f"Expected 409, got {status}"

    # POST item with scenario "pc-409"
    status, _ = request("POST", "/api/v1/groups", body={"name":"test","description":"desc"}, scenario="pc-409")
    assert status == 409, f"Expected 409 for POST conflict, got {status}"

    # PUT item with scenario "pc-404"
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}", body={"name":"test","description":"desc"}, scenario="pc-404")
    assert status == 404, f"Expected 404 for PUT not-found, got {status}"

    # DELETE item with scenario "pc-404"
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}", scenario="pc-404")
    assert status == 404, f"Expected 404 for DELETE not-found, got {status}"

    print("✅ Forwarded downstream failure tests passed")

def test_delete_orchestration():
    print("Testing DELETE /groups/{id} config-raw orchestrations...")
    
    # Happy path config-raw
    status, body = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}", scenario="config-raw")
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
        assert any(len(entry) > 1 and entry[1] == "wifi.ssid" for entry in config_raw), "Missing wifi.ssid from merged config-raw"
        assert any(len(entry) > 1 and entry[1] == "parental_control.ci_rule" for entry in config_raw), "Missing parental_control.ci_rule from merged config-raw"
        assert not any(len(entry) > 1 and entry[1] == "parental_control.old_rule" for entry in config_raw), "parental_control.old_rule was not deleted"
        print("✅ DELETE config-raw happy path passed")

    # J) DELETE item with scenario "delete-config-raw-prov-502"
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}", scenario="delete-config-raw-prov-502")
    assert status == 502, f"Expected 502 for provisioning failure, got {status}"
    
    # K) DELETE item with scenario "delete-config-raw-gw-get-malformed"
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}", scenario="delete-config-raw-gw-get-malformed")
    assert status == 500, f"Expected 500 for gw-get malformed failure, got {status}"
    
    # L) DELETE item with scenario "delete-config-raw-gw-configure-502"
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}", scenario="delete-config-raw-gw-configure-502")
    assert status == 502, f"Expected 502 for gw configure failure, got {status}"
    
    print("✅ DELETE config-raw error scenarios passed")

if __name__ == "__main__":
    print("Starting integration tests...")
    try:
        test_get_groups()
        test_post_groups()
        test_get_group_by_id()
        test_put_groups()
        test_delete_groups_normal()
        

        test_forwarded_failures()
        test_delete_orchestration()
        
        print("🎉 All integration tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
