import urllib.request
import urllib.error
import json
import os
import sys
import ssl
import re

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
VALID_GROUP_ID = "11111111-1111-4111-8111-111111111111"

HTTPS_CONTEXT = ssl._create_unverified_context()

def open_url(req_or_url):
    url = req_or_url.full_url if hasattr(req_or_url, "full_url") else req_or_url
    if str(url).startswith("https://"):
        return urllib.request.urlopen(req_or_url, context=HTTPS_CONTEXT)
    return urllib.request.urlopen(req_or_url)

def set_scenario(scenario):
    req = urllib.request.Request(
        f"{FAKE_URL}/set-scenario",
        data=json.dumps({"scenario": scenario}).encode(),
        method="POST"
    )
    open_url(req)

def reset_observations():
    req = urllib.request.Request(f"{FAKE_URL}/reset-observations", data=b"", method="POST")
    open_url(req)
    set_scenario("normal")

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
    # Missing auth
    reset_observations()
    status, _ = request("GET", "/api/v1/groups", headers={})
    assert status == 403, f"Expected 403 for missing auth, got {status}"
    check_no_observations("GET /groups missing auth")

    # Bad auth
    reset_observations()
    status, _ = request("GET", "/api/v1/groups", headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth, got {status}"
    check_no_observations("GET /groups bad auth")
    print("✅ Auth tests passed")

def test_local_validation():
    print("Testing Local Request Validation...")

    # A) GET invalid UUID
    reset_observations()
    status, _ = request("GET", "/api/v1/groups/12345")
    assert status == 400, f"Expected 400 for invalid UUID, got {status}"
    check_no_observations("GET invalid UUID")
    
    # B) POST unknown field
    reset_observations()
    status, _ = request("POST", "/api/v1/groups", body={"name":"test","description":"desc","extra":"bad"})
    assert status == 400, f"Expected 400 for unknown field, got {status}"
    check_no_observations("POST unknown field")
    
    # C) POST missing name
    reset_observations()
    status, _ = request("POST", "/api/v1/groups", body={"description":"desc"})
    assert status == 400, f"Expected 400 for missing name, got {status}"
    check_no_observations("POST missing name")
    
    # D) POST empty name
    reset_observations()
    status, _ = request("POST", "/api/v1/groups", body={"name":"   ","description":"desc"})
    assert status == 400, f"Expected 400 for empty name, got {status}"
    check_no_observations("POST empty name")
    
    # E) POST invalid description type
    reset_observations()
    status, _ = request("POST", "/api/v1/groups", body={"name":"test","description":123})
    assert status == 400, f"Expected 400 for invalid description type, got {status}"
    check_no_observations("POST invalid description type")
    
    # F) POST malformed JSON
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/groups", data=b"{bad-json", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="POST")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for malformed POST json, got {status}"
    check_no_observations("POST malformed json")

    # G) PUT with only name (description is optional) — must be forwarded downstream, not rejected locally
    # VALID_GROUP_ID does not exist in the fake DB, so downstream returns 404.
    # A 404 from downstream is proof the request passed local validation and was forwarded.
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}", body={"name":"test"})
    assert status == 404, f"Expected 404 (forwarded to downstream, group not found), got {status}"
    check_downstream_called("PUT with only name (description is optional)")

    # G2) PUT with invalid description type (integer) — must still be rejected locally with 400
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}", body={"name":"test", "description": 123})
    assert status == 400, f"Expected 400 for invalid description type on PUT, got {status}"
    check_no_observations("PUT invalid description type")
    
    # H) PUT unknown field
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{VALID_GROUP_ID}", body={"name":"test","description":"desc","extra":"bad"})
    assert status == 400, f"Expected 400 for unknown field on PUT, got {status}"
    check_no_observations("PUT unknown field")

    # I) PUT malformed JSON
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}", data=b"{bad-json", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="PUT")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for malformed PUT json, got {status}"
    check_no_observations("PUT malformed json")

    # J) POST missing body
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/groups", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="POST")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for POST missing body, got {status}"
    check_no_observations("POST missing body")

    # K) PUT missing body
    reset_observations()
    req = urllib.request.Request(f"{USERPORTAL_URL}/api/v1/groups/{VALID_GROUP_ID}", headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"}, method="PUT")
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for PUT missing body, got {status}"
    check_no_observations("PUT missing body")

    print("✅ Local validation tests passed")

def test_forwarded_payloads():
    print("Testing Group Forwarded Downstream Payloads...")

    # Case 1: POST /api/v1/groups with description omitted
    reset_observations()
    status, res = request("POST", "/api/v1/groups", body={"name": "Omitted desc group"})
    assert status == 200, f"Expected 200, got {status}"
    group_id = res.get("id")
    assert group_id is not None
    calls = get_observations()
    post_calls = [c for c in calls if c["method"] == "POST" and "/groups" in c["path"]]
    assert len(post_calls) > 0
    body = post_calls[0].get("body", {})
    assert "description" not in body, f"description should be omitted in downstream payload: {body}"

    # Case 2: POST /api/v1/groups with description: null
    reset_observations()
    status, _ = request("POST", "/api/v1/groups", body={"name": "Null desc group", "description": None})
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    post_calls = [c for c in calls if c["method"] == "POST" and "/groups" in c["path"]]
    assert len(post_calls) > 0
    body = post_calls[0].get("body", {})
    assert "description" in body and body["description"] is None, f"description should be explicitly null in downstream payload: {body}"

    # Case 3: POST /api/v1/groups with description string
    reset_observations()
    status, _ = request("POST", "/api/v1/groups", body={"name": "String desc group", "description": "some description"})
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    post_calls = [c for c in calls if c["method"] == "POST" and "/groups" in c["path"]]
    assert len(post_calls) > 0
    body = post_calls[0].get("body", {})
    assert body.get("description") == "some description", f"description should match: {body}"

    # Case 4: PUT /api/v1/groups/{id} with description omitted
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{group_id}", body={"name": "Updated name"})
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    put_calls = [c for c in calls if c["method"] == "PUT" and f"/groups/{group_id}" in c["path"]]
    assert len(put_calls) > 0
    body = put_calls[0].get("body", {})
    assert "description" not in body, f"description should be omitted in downstream PUT payload: {body}"

    # Case 5: PUT /api/v1/groups/{id} with description: null
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{group_id}", body={"name": "Updated name", "description": None})
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    put_calls = [c for c in calls if c["method"] == "PUT" and f"/groups/{group_id}" in c["path"]]
    assert len(put_calls) > 0
    body = put_calls[0].get("body", {})
    assert "description" in body and body["description"] is None, f"description should be null in downstream PUT payload: {body}"

    # Case 6: PUT /api/v1/groups/{id} with description string
    reset_observations()
    status, _ = request("PUT", f"/api/v1/groups/{group_id}", body={"name": "Updated name", "description": "new description"})
    assert status == 200, f"Expected 200, got {status}"
    calls = get_observations()
    put_calls = [c for c in calls if c["method"] == "PUT" and f"/groups/{group_id}" in c["path"]]
    assert len(put_calls) > 0
    body = put_calls[0].get("body", {})
    assert body.get("description") == "new description", f"description should match: {body}"

    print("✅ Group forwarded payload tests passed")

UUID_REGEX = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")
DATETIME_REGEX = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$")
ALLOWED_GROUP_LIST_FIELDS = {
    "id", "subscriber_id", "group_config_index", "name",
    "description", "created_at", "updated_at", "config-raw", "device_count"
}

def test_response_schema():
    print("Testing Group Response Schema Contracts (GroupListItem focused validation)...")
    set_scenario("groups-device-counts")
    status, body = request("GET", "/api/v1/groups")
    assert status == 200, f"Expected 200, got {status}. Body: {body}"
    assert isinstance(body, list), f"Expected list of groups, got {type(body)}"
    assert len(body) == 2, f"Expected 2 groups, got {len(body)}"

    required_fields = [
        "id", "subscriber_id", "group_config_index", "name",
        "created_at", "updated_at", "device_count"
    ]
    for item in body:
        assert isinstance(item, dict), f"Expected group object, got {type(item)}"

        # Validate additionalProperties: false
        unexpected = set(item.keys()) - ALLOWED_GROUP_LIST_FIELDS
        assert not unexpected, f"Unexpected properties in GroupListItem: {unexpected}"

        for field in required_fields:
            assert field in item, f"Missing required GroupListItem field '{field}' in response item: {item}"

        assert isinstance(item["device_count"], int) and not isinstance(item["device_count"], bool), (
            f"device_count must be int, got {type(item['device_count'])}"
        )
        assert item["device_count"] >= 0, f"device_count must be >= 0: {item['device_count']}"
        assert isinstance(item["id"], str) and UUID_REGEX.match(item["id"]), f"id must be valid UUID: {item.get('id')}"
        assert isinstance(item["subscriber_id"], str) and UUID_REGEX.match(item["subscriber_id"]), (
            f"subscriber_id must be valid UUID: {item.get('subscriber_id')}"
        )
        assert isinstance(item["group_config_index"], int) and not isinstance(item["group_config_index"], bool)
        assert isinstance(item["name"], str)
        assert isinstance(item["created_at"], str) and DATETIME_REGEX.match(item["created_at"]), (
            f"created_at must be date-time: {item.get('created_at')}"
        )
        assert isinstance(item["updated_at"], str) and DATETIME_REGEX.match(item["updated_at"]), (
            f"updated_at must be date-time: {item.get('updated_at')}"
        )
        if item.get("description") is not None:
            assert isinstance(item["description"], str), f"description must be str or None: {item.get('description')}"
        if item.get("config-raw") is not None:
            assert isinstance(item["config-raw"], list), f"config-raw must be list or None: {item.get('config-raw')}"

    assert body[0]["device_count"] == 3
    assert body[1]["device_count"] == 0

    reset_observations()
    print("✅ Group response schema contract tests passed (GroupListItem format & fields validated)")

if __name__ == "__main__":
    print("Starting contract tests...")
    try:
        test_auth_checks()
        test_local_validation()
        test_forwarded_payloads()
        test_response_schema()
        print("🎉 All contract tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
