import urllib.request
import urllib.error
import json
import os
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")

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

# --- Higher-level scenario helpers ---

def assert_auth_rejected(method, path, body=None):
    # 1. Missing auth
    reset_observations()
    status, _ = request(method, path, body=body, headers={})
    assert status == 403, f"Expected 403 for missing auth on {method} {path}, got {status}"
    check_no_observations(f"{method} {path} missing auth")

    # 2. Bad token auth
    reset_observations()
    status, _ = request(method, path, body=body, headers={"Authorization": "Bearer bad-token"})
    assert status == 403, f"Expected 403 for bad auth on {method} {path}, got {status}"
    check_no_observations(f"{method} {path} bad auth")

def assert_missing_operator_rejected(method, path, body=None):
    reset_observations()
    status, _ = request(method, path, body=body, headers={"Authorization": "Bearer no-owner-token"})
    assert status == 403, f"Expected 403 for missing operator on {method} {path}, got {status}"
    check_no_observations(f"{method} {path} missing operator")

def assert_missing_body_rejected(method, path):
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}{path}",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method=method
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for missing body on {method} {path}, got {status}"
    check_no_observations(f"{method} {path} missing body")

def assert_malformed_json_rejected(method, path):
    reset_observations()
    req = urllib.request.Request(
        f"{USERPORTAL_URL}{path}",
        data=b"{bad-json",
        headers={"Authorization": "Bearer dummy-test-token", "Content-Type": "application/json"},
        method=method
    )
    try:
        with open_url(req) as response:
            status = response.status
    except urllib.error.HTTPError as e:
        status = e.code
    assert status == 400, f"Expected 400 for malformed JSON on {method} {path}, got {status}"
    check_no_observations(f"{method} {path} malformed JSON")

def assert_unknown_field_rejected(method, path, body):
    reset_observations()
    body_copy = dict(body)
    body_copy["extra"] = "bad"
    status, _ = request(method, path, body=body_copy)
    assert status == 400, f"Expected 400 for unknown field on {method} {path}, got {status}"
    check_no_observations(f"{method} {path} unknown field")

def assert_local_validation_failed(method, path, body=None, test_name="local validation"):
    reset_observations()
    status, _ = request(method, path, body=body)
    assert status == 400, f"Expected 400 for {test_name}, got {status}"
    check_no_observations(test_name)

def run_scenario_failure_case(scenario_name, method, path, body, expected_status, test_name):
    set_scenario(scenario_name)
    reset_observations()
    status, res_body = request(method, path, body=body)
    assert status == expected_status, f"{test_name}: Expected status {expected_status}, got {status}. Response: {res_body}"
    check_no_observations(test_name)
