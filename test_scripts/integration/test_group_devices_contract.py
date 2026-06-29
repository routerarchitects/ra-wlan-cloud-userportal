import sys
from parental_control_test_helpers import (
    set_scenario, assert_auth_rejected, assert_unknown_field_rejected,
    assert_missing_body_rejected, assert_malformed_json_rejected,
    assert_local_validation_failed, run_scenario_failure_case
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

def test_dependency_validation_failures():
    print("Testing local topology/provisioning dependency validation failures on POST...")

    path = f"/api/v1/groups/{VALID_GROUP_ID}/devices"
    body = {"client_mac": VALID_CLIENT_MAC}

    run_scenario_failure_case("prov-502", "POST", path, body, 500, "provisioning lookup failure")
    run_scenario_failure_case("inventory-missing", "POST", path, body, 400, "inventory missing")
    run_scenario_failure_case("inventory-venue-empty", "POST", path, body, 400, "inventory venue empty")
    run_scenario_failure_case("venue-missing", "POST", path, body, 400, "venue missing")
    run_scenario_failure_case("venue-fail", "POST", path, body, 500, "venue lookup failure")
    run_scenario_failure_case("board-missing", "POST", path, body, 400, "board missing or empty")
    run_scenario_failure_case("topology-fail", "POST", path, body, 500, "topology fetch failure")
    run_scenario_failure_case("mac-not-present", "POST", path, body, 400, "MAC not present in topology")
    run_scenario_failure_case("topology-malformed", "POST", path, body, 500, "malformed topology payload")

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
