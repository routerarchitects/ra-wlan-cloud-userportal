import sys
import json
from urllib.parse import urlparse

from parental_control_test_helpers import (
    FAKE_URL,
    open_url,
    request,
    reset_observations,
    set_scenario,
    assert_auth_rejected,
    assert_unknown_field_rejected,
    assert_missing_body_rejected,
    assert_malformed_json_rejected,
    assert_local_validation_failed,
)

VALID_GROUP_ID = "11111111-1111-4111-8111-111111111111"
VALID_CLIENT_MAC = "AA:BB:CC:DD:EE:FF"
INVALID_MAC = "ZZZZ"
INVALID_UUID = "12345"

# ---------------------------------------------------------------------------
# Auth rejection tests
# ---------------------------------------------------------------------------

def test_auth_checks():
    print("Testing Auth Rejections...")
    assert_auth_rejected("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices")
    assert_auth_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={"client_mac": VALID_CLIENT_MAC})
    assert_auth_rejected("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{VALID_CLIENT_MAC}")
    assert_auth_rejected("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{VALID_CLIENT_MAC}")
    print("✅ Auth tests passed")

# ---------------------------------------------------------------------------
# Local validation tests — none of these should reach downstream
# ---------------------------------------------------------------------------

def test_local_validation():
    print("Testing Local Request Validation...")

    # --- GET /groups/{group_id}/devices ---
    assert_local_validation_failed("GET", f"/api/v1/groups/{INVALID_UUID}/devices", test_name="GET group-devices invalid group UUID")
    assert_local_validation_failed("GET", f"/api/v1/groups/{INVALID_UUID}/devices/{VALID_CLIENT_MAC}", test_name="GET group-device invalid group UUID")
    assert_local_validation_failed("GET", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{INVALID_MAC}", test_name="GET group-device invalid MAC")

    # --- POST /groups/{group_id}/devices ---
    assert_local_validation_failed("POST", f"/api/v1/groups/{INVALID_UUID}/devices", body={"client_mac": VALID_CLIENT_MAC}, test_name="POST group-devices invalid group UUID")
    assert_local_validation_failed("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={}, test_name="POST group-devices missing client_mac")
    assert_local_validation_failed("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={"client_mac": None}, test_name="POST group-devices null client_mac")
    assert_local_validation_failed("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={"client_mac": INVALID_MAC}, test_name="POST group-devices invalid MAC format")
    assert_unknown_field_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices", body={"client_mac": VALID_CLIENT_MAC})
    assert_missing_body_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices")
    assert_malformed_json_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/devices")

    # --- DELETE /groups/{group_id}/devices/{client_mac} ---
    assert_local_validation_failed("DELETE", f"/api/v1/groups/{INVALID_UUID}/devices/{VALID_CLIENT_MAC}", test_name="DELETE group-device invalid group UUID")
    assert_local_validation_failed("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/devices/{INVALID_MAC}", test_name="DELETE group-device invalid MAC")

    print("✅ Local validation tests passed")

def test_no_topology_validation_on_post():
    print("Testing POST /groups/{group_id}/devices performs no topology/venue/inventory validation...")
    status, group_res = request("POST", "/api/v1/groups", body={"name": "no-topo-group", "description": "desc"})
    assert status == 200, f"Expected 200 on group create, got {status}"
    group_id = group_res["id"]

    set_scenario("config-raw")
    reset_observations()

    status, res_body = request("POST", f"/api/v1/groups/{group_id}/devices", body={"client_mac": VALID_CLIENT_MAC})
    assert status == 200, f"Expected 200 on POST group device, got {status}. Body: {res_body}"

    with open_url(f"{FAKE_URL}/observations") as r:
        obs = json.loads(r.read())
        calls = obs.get("calls", [])

        # Assert no calls to inventory, venue, location, topology
        for c in calls:
            path = urlparse(c["path"]).path

            assert not path.startswith("/api/v1/inventory/"), \
                f"Forbidden call to inventory: {c['path']}"
            assert path != "/api/v1/venue" and not path.startswith("/api/v1/venue/"), \
                f"Forbidden call to venue: {c['path']}"
            assert not path.startswith("/api/v1/location/"), \
                f"Forbidden call to location: {c['path']}"
            assert not path.startswith("/api/v1/topology"), \
                f"Forbidden call to topology: {c['path']}"

        # Assert parental-control call for group-device POST path
        pc_calls = [
            c for c in calls
            if c["method"] == "POST"
            and c["path"].startswith("/api/v1/subscribers/")
            and c["path"].endswith(f"/groups/{group_id}/devices")
        ]
        assert len(pc_calls) > 0, f"Expected parental-control call for group-device POST, got {calls}"

    set_scenario("normal")
    print("✅ No topology validation on POST passed")

if __name__ == "__main__":
    print("Starting group-devices contract tests...")
    try:
        test_auth_checks()
        test_local_validation()
        test_no_topology_validation_on_post()
        print("🎉 All group-devices contract tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
