import urllib.request
import urllib.error
import json
import os
import sys
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
VALID_GROUP_ID = "11111111-1111-4111-8111-111111111111"
VALID_CLIENT_MAC = "AA:BB:CC:DD:EE:FF"
INVALID_MAC = "ZZZZ"
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

def set_scenario(scenario_name):
    req = urllib.request.Request(
        f"{FAKE_URL}/set-scenario",
        data=json.dumps({"scenario": scenario_name}).encode(),
        method="POST"
    )
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
        downstream_calls = [c for c in obs["calls"] if "/subscribers/" in c["path"] or "configure" in c["path"]]
        assert len(downstream_calls) == 0, f"{test_name}: Downstream unexpectedly called: {downstream_calls}"

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
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices", headers={})
    assert status == 403, f"Expected 403 for missing auth on GET devices, got {status}"
    check_no_observations("GET group-devices missing auth")

    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices",
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on GET devices, got {status}"
    check_no_observations("GET group-devices bad auth")

    # POST
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices",
                        body={"client_mac": VALID_CLIENT_MAC}, headers={})
    assert status == 403, f"Expected 403 for missing auth on POST devices, got {status}"
    check_no_observations("POST group-devices missing auth")

    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices",
                        body={"client_mac": VALID_CLIENT_MAC},
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on POST devices, got {status}"
    check_no_observations("POST group-devices bad auth")

    # GET single
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{VALID_CLIENT_MAC}", headers={})
    assert status == 403, f"Expected 403 for missing auth on GET single device, got {status}"
    check_no_observations("GET single device missing auth")

    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{VALID_CLIENT_MAC}",
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on GET single device, got {status}"
    check_no_observations("GET single device bad auth")

    # DELETE
    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{VALID_CLIENT_MAC}", headers={})
    assert status == 403, f"Expected 403 for missing auth on DELETE device, got {status}"
    check_no_observations("DELETE device missing auth")

    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{VALID_CLIENT_MAC}",
                        headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on DELETE device, got {status}"
    check_no_observations("DELETE device bad auth")

    print("✅ Auth tests passed")


# ---------------------------------------------------------------------------
# Local validation tests — none of these should reach downstream
# ---------------------------------------------------------------------------

def test_local_validation():
    print("Testing Local Request Validation...")

    # --- GET /groups/{group_id}/devices ---

    # Invalid group_id UUID on GET list
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{INVALID_UUID}/devices")
    assert status == 400, f"Expected 400 for invalid group UUID on GET devices, got {status}"
    check_no_observations("GET group-devices invalid group UUID")

    # Invalid group_id UUID on GET single device
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{INVALID_UUID}/devices/{VALID_CLIENT_MAC}")
    assert status == 400, f"Expected 400 for invalid group UUID on GET device, got {status}"
    check_no_observations("GET group-device invalid group UUID")

    # Invalid MAC on GET single device
    reset_observations()
    status, _ = request("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{INVALID_MAC}")
    assert status == 400, f"Expected 400 for invalid MAC on GET device, got {status}"
    check_no_observations("GET group-device invalid MAC")

    # --- POST /groups/{group_id}/devices ---

    # Invalid group_id UUID on POST
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{INVALID_UUID}/devices",
                        body={"client_mac": VALID_CLIENT_MAC})
    assert status == 400, f"Expected 400 for invalid group UUID on POST devices, got {status}"
    check_no_observations("POST group-devices invalid group UUID")

    # Missing client_mac
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={})
    assert status == 400, f"Expected 400 for missing client_mac, got {status}"
    check_no_observations("POST group-devices missing client_mac")

    # Null client_mac
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices",
                        body={"client_mac": None})
    assert status == 400, f"Expected 400 for null client_mac, got {status}"
    check_no_observations("POST group-devices null client_mac")

    # Invalid MAC format
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices",
                        body={"client_mac": INVALID_MAC})
    assert status == 400, f"Expected 400 for invalid MAC format, got {status}"
    check_no_observations("POST group-devices invalid MAC format")

    # Unknown field in body
    reset_observations()
    status, _ = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices",
                        body={"client_mac": VALID_CLIENT_MAC, "extra": "bad"})
    assert status == 400, f"Expected 400 for unknown field on POST devices, got {status}"
    check_no_observations("POST group-devices unknown field")

    # Missing body entirely
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}/devices",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method="POST"
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for POST with missing body, got {status}"
    check_no_observations("POST group-devices missing body")

    # Malformed JSON
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}/devices",
        data=b"{bad-json",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method="POST"
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for POST with malformed JSON, got {status}"
    check_no_observations("POST group-devices malformed JSON")

    # --- DELETE /groups/{group_id}/devices/{client_mac} ---

    # Invalid group UUID on DELETE
    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{INVALID_UUID}/devices/{VALID_CLIENT_MAC}")
    assert status == 400, f"Expected 400 for invalid group UUID on DELETE device, got {status}"
    check_no_observations("DELETE group-device invalid group UUID")

    # Invalid MAC on DELETE
    reset_observations()
    status, _ = request("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{INVALID_MAC}")
    assert status == 400, f"Expected 400 for invalid MAC on DELETE device, got {status}"
    check_no_observations("DELETE group-device invalid MAC")

    print("✅ Local validation tests passed")

def test_dependency_validation_failures():
    print("Testing local topology/provisioning dependency validation failures on POST...")

    # helper to set scenario, reset observations, call POST, assert status, check no observations
    def run_dep_fail_case(scenario_name, expected_status, test_name):
        set_scenario(scenario_name)
        reset_observations()
        status, body = request("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={"client_mac": VALID_CLIENT_MAC})
        assert status == expected_status, f"{test_name}: Expected status {expected_status}, got {status}. Response: {body}"
        check_no_observations(test_name)

    # 1. provisioning lookup failure
    run_dep_fail_case("prov-502", 500, "provisioning lookup failure")

    # 2a. inventory missing
    run_dep_fail_case("inventory-missing", 400, "inventory missing")

    # 2b. inventory lookup failure (inventory venue is empty)
    run_dep_fail_case("inventory-venue-empty", 400, "inventory venue empty")

    # 3a. venue missing
    run_dep_fail_case("venue-missing", 400, "venue missing")

    # 3b. venue lookup failure
    run_dep_fail_case("venue-fail", 500, "venue lookup failure")

    # 4. board missing or empty
    run_dep_fail_case("board-missing", 400, "board missing or empty")

    # 5. topology fetch failure
    run_dep_fail_case("topology-fail", 500, "topology fetch failure")

    # 6. MAC not present in topology
    run_dep_fail_case("mac-not-present", 400, "MAC not present in topology")

    # 7. malformed topology payload shape
    run_dep_fail_case("topology-malformed", 500, "malformed topology payload")

    # Clean up by resetting to normal
    set_scenario("normal")
    print("✅ Dependency validation failure tests passed")

if __name__ == "__main__":
    print("Starting group-devices contract tests...")
    try:
        test_auth_checks()
        test_local_validation()
        test_dependency_validation_failures()
        print("🎉 All group-devices contract tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
