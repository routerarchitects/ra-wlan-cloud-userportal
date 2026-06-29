import sys
from parental_control_test_helpers import (
    assert_auth_rejected, assert_missing_operator_rejected,
    assert_unknown_field_rejected, assert_missing_body_rejected,
    assert_malformed_json_rejected, assert_local_validation_failed
)

VALID_GROUP_ID = "11111111-1111-4111-8111-111111111111"
VALID_SCHEDULE_ID = "22222222-2222-4222-8222-222222222222"
INVALID_UUID = "12345"

# ---------------------------------------------------------------------------
# Auth rejection tests
# ---------------------------------------------------------------------------

def test_auth_checks():
    print("Testing Auth Rejections...")
    assert_auth_rejected("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules")
    assert_auth_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_id": VALID_SCHEDULE_ID})
    assert_auth_rejected("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": [VALID_SCHEDULE_ID]})
    assert_auth_rejected("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}")
    assert_auth_rejected("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}")
    print("✅ Auth tests passed")

# ---------------------------------------------------------------------------
# Local validation tests — none of these should reach downstream
# ---------------------------------------------------------------------------

def test_local_validation():
    print("Testing Local Request Validation...")

    # --- GET /groups/{group_id}/schedules ---
    assert_local_validation_failed("GET", f"/api/v1/groups/{INVALID_UUID}/schedules", test_name="GET group-schedules invalid group UUID")
    assert_local_validation_failed("GET", f"/api/v1/groups/{INVALID_UUID}/schedules/{VALID_SCHEDULE_ID}", test_name="GET group-schedule invalid group UUID")
    assert_local_validation_failed("GET", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{INVALID_UUID}", test_name="GET group-schedule invalid schedule UUID")

    # --- POST /groups/{group_id}/schedules ---
    assert_local_validation_failed("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={}, test_name="POST group-schedules missing schedule_id")
    assert_local_validation_failed("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_id": None}, test_name="POST group-schedules null schedule_id")
    assert_local_validation_failed("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_id": INVALID_UUID}, test_name="POST group-schedules non-UUID schedule_id")
    assert_unknown_field_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_id": VALID_SCHEDULE_ID})
    assert_missing_body_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules")
    assert_malformed_json_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules")

    # --- PUT /groups/{group_id}/schedules ---
    assert_local_validation_failed("PUT", f"/api/v1/groups/{INVALID_UUID}/schedules", body={"schedule_ids": [VALID_SCHEDULE_ID]}, test_name="PUT group-schedules invalid group UUID")
    assert_local_validation_failed("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={}, test_name="PUT group-schedules missing schedule_ids")
    assert_local_validation_failed("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": None}, test_name="PUT group-schedules null schedule_ids")
    assert_local_validation_failed("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": VALID_SCHEDULE_ID}, test_name="PUT group-schedules non-array schedule_ids")
    assert_local_validation_failed("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": [INVALID_UUID]}, test_name="PUT group-schedules non-UUID entry")
    assert_local_validation_failed("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": [VALID_SCHEDULE_ID, VALID_SCHEDULE_ID]}, test_name="PUT group-schedules duplicate UUIDs")
    assert_unknown_field_rejected("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": [VALID_SCHEDULE_ID]})
    assert_missing_body_rejected("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules")
    assert_malformed_json_rejected("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules")

    # --- DELETE /groups/{group_id}/schedules/{schedule_id} ---
    assert_local_validation_failed("DELETE", f"/api/v1/groups/{INVALID_UUID}/schedules/{VALID_SCHEDULE_ID}", test_name="DELETE group-schedule invalid group UUID")
    assert_local_validation_failed("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{INVALID_UUID}", test_name="DELETE group-schedule invalid schedule UUID")

    print("✅ Local validation tests passed")

def test_missing_operator():
    print("Testing Missing Operator Rejection...")
    assert_missing_operator_rejected("POST", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_id": VALID_SCHEDULE_ID})
    assert_missing_operator_rejected("PUT", f"/api/v1/groups/{VALID_GROUP_ID}/schedules", body={"schedule_ids": [VALID_SCHEDULE_ID]})
    assert_missing_operator_rejected("DELETE", f"/api/v1/groups/{VALID_GROUP_ID}/schedules/{VALID_SCHEDULE_ID}")
    print("✅ Missing operator tests passed")

if __name__ == "__main__":
    print("Starting group-schedules contract tests...")
    try:
        test_auth_checks()
        test_local_validation()
        test_missing_operator()
        print("🎉 All group-schedules contract tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
