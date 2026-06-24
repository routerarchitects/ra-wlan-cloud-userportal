import urllib.request
import urllib.error
import json
import os
import sys
import ssl

USERPORTAL_URL = os.environ.get("USERPORTAL_URL", "http://localhost:16006")
FAKE_URL = os.environ.get("FAKE_URL", "http://127.0.0.1:8080")
TOKEN = "dummy-test-token"

# A MAC that the fake topology includes for successful validation
TOPOLOGY_KNOWN_MAC = "AA:BB:CC:DD:EE:FF"
# A MAC that is a valid format but NOT in the fake topology
TOPOLOGY_UNKNOWN_MAC = "11:22:33:44:55:66"

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
        if isinstance(body, (dict, list)):
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
        except Exception:
            return e.code, res_body

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def create_group(name="test-group"):
    status, body = request("POST", "/api/v1/groups", body={"name": name, "description": "desc"})
    assert status == 200, f"Expected 200 on group create, got {status}. Body: {body}"
    return body["id"]

def get_observations():
    with open_url(f"{FAKE_URL}/observations") as r:
        return json.loads(r.read())


# ---------------------------------------------------------------------------
# Happy-path Read-after-Write & State Verification
# ---------------------------------------------------------------------------

def test_group_device_crud_state_lifecycle():
    print("Testing Group Device CRUD State Lifecycle (Read-after-Write)...")
    gid = create_group("gd-crud-lifecycle")

    # 1. Initially list should be empty
    status, list_body = request("GET", f"/api/v1/groups/{gid}/devices")
    assert status == 200, f"Expected 200, got {status}"
    assert len(list_body) == 0, f"Expected empty device list, got {list_body}"

    # 2. GET single device that does not exist -> 404 (State-based not found)
    status, _ = request("GET", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}")
    assert status == 404, f"Expected state-based 404 for unlinked device, got {status}"

    # 3. POST link device -> 200
    status, post_body = request("POST", f"/api/v1/groups/{gid}/devices",
                               body={"client_mac": TOPOLOGY_KNOWN_MAC},
                               scenario="config-raw")
    assert status == 200, f"Expected 200 on POST, got {status}"
    assert "config-raw" not in post_body, "config-raw should be stripped from response"

    # 4. GET list should contain the linked device
    status, list_body = request("GET", f"/api/v1/groups/{gid}/devices")
    assert status == 200
    assert any(d.get("client_mac") == TOPOLOGY_KNOWN_MAC for d in list_body), \
        f"Linked device {TOPOLOGY_KNOWN_MAC} not found in list: {list_body}"

    # 5. GET single device should return it
    status, get_body = request("GET", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}")
    assert status == 200
    assert get_body.get("client_mac") == TOPOLOGY_KNOWN_MAC, f"Expected {TOPOLOGY_KNOWN_MAC}, got {get_body}"

    # 6. DELETE unlink device -> 200
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}",
                        scenario="config-raw")
    assert status == 200, f"Expected 200 on DELETE, got {status}"

    # 7. GET single device should now be 404 (Real post-delete 404 flow)
    status, _ = request("GET", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}")
    assert status == 404, f"Expected 404 after delete, got {status}"

    # 8. GET list should be empty again
    status, list_body = request("GET", f"/api/v1/groups/{gid}/devices")
    assert status == 200
    assert len(list_body) == 0, f"Expected empty list after delete, got {list_body}"

    print("✅ Group Device CRUD State Lifecycle passed")


# ---------------------------------------------------------------------------
# POST — topology validation required before downstream write
# ---------------------------------------------------------------------------

def test_post_group_device_topology_validation():
    print("Testing POST /groups/{id}/devices topology validation...")
    gid = create_group("gd-topo-test")

    # MAC not in topology → 400 MacNotPresentInTopology
    status, _ = request("POST", f"/api/v1/groups/{gid}/devices",
                        body={"client_mac": TOPOLOGY_UNKNOWN_MAC})
    assert status == 400, f"Expected 400 for MAC not in topology, got {status}"

    obs = get_observations()
    # subscriberDevice (prov) must have been called (topology chain started)
    assert any("subscriberDevice" in c["path"] for c in obs["calls"]), \
        "Expected prov subscriberDevice call for topology validation"
    # Parental-control group-devices must NOT have been called (topology rejected)
    assert not any("/groups/" in c["path"] and "/devices" in c["path"] for c in obs["calls"]), \
        "Parental-control group-devices should NOT be called when topology rejects"

    print("✅ POST topology validation (MAC not in topology → 400) passed")


# ---------------------------------------------------------------------------
# POST — downstream failure scenarios (config-raw required, gw errors)
# ---------------------------------------------------------------------------

def test_post_group_device_missing_config_raw():
    """If downstream write succeeds but returns no config-raw, UserPortal must return 500."""
    print("Testing POST /groups/{id}/devices with missing config-raw → 500...")
    gid = create_group("gd-no-cr-test")
    # "normal" scenario: fake returns 200 but no config-raw field in response
    status, _ = request("POST", f"/api/v1/groups/{gid}/devices",
                        body={"client_mac": TOPOLOGY_KNOWN_MAC},
                        scenario="normal")
    assert status == 500, f"Expected 500 when downstream response missing config-raw, got {status}"
    print("✅ POST missing config-raw → 500 passed")


def test_post_group_device_gw_failures():
    print("Testing POST /groups/{id}/devices gateway failure scenarios...")
    gid = create_group("gd-gw-fail-test")

    status, _ = request("POST", f"/api/v1/groups/{gid}/devices",
                        body={"client_mac": TOPOLOGY_KNOWN_MAC},
                        scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw GET failure, got {status}"

    status, _ = request("POST", f"/api/v1/groups/{gid}/devices",
                        body={"client_mac": TOPOLOGY_KNOWN_MAC},
                        scenario="delete-config-raw-gw-get-malformed")
    assert status == 500, f"Expected 500 for gw GET malformed, got {status}"

    status, _ = request("POST", f"/api/v1/groups/{gid}/devices",
                        body={"client_mac": TOPOLOGY_KNOWN_MAC},
                        scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure, got {status}"

    print("✅ POST gateway failure scenarios passed")


# ---------------------------------------------------------------------------
# DELETE — failure scenarios
# ---------------------------------------------------------------------------

def test_delete_group_device_missing_config_raw():
    print("Testing DELETE /groups/{id}/devices/{mac} missing config-raw → 500...")
    gid = create_group("gd-del-no-cr")
    # First link it
    request("POST", f"/api/v1/groups/{gid}/devices",
            body={"client_mac": TOPOLOGY_KNOWN_MAC}, scenario="config-raw")

    # normal scenario → delete succeeds downstream but returns no config-raw
    status, _ = request("DELETE", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}",
                        scenario="normal")
    assert status == 500, f"Expected 500 when DELETE response missing config-raw, got {status}"
    print("✅ DELETE missing config-raw → 500 passed")


def test_delete_group_device_gw_failures():
    print("Testing DELETE /groups/{id}/devices/{mac} gateway failure scenarios...")
    gid = create_group("gd-del-gw-fail")
    # First link it
    request("POST", f"/api/v1/groups/{gid}/devices",
            body={"client_mac": TOPOLOGY_KNOWN_MAC}, scenario="config-raw")

    status, _ = request("DELETE", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}",
                        scenario="delete-config-raw-prov-502")
    assert status == 500, f"Expected 500 for prov failure on DELETE, got {status}"

    status, _ = request("DELETE", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}",
                        scenario="delete-config-raw-gw-get-502")
    assert status == 500, f"Expected 500 for gw GET failure on DELETE, got {status}"

    status, _ = request("DELETE", f"/api/v1/groups/{gid}/devices/{TOPOLOGY_KNOWN_MAC}",
                        scenario="delete-config-raw-gw-configure-502")
    assert status == 500, f"Expected 500 for gw configure failure on DELETE, got {status}"

    print("✅ DELETE gateway failure scenarios passed")


def test_forwarded_404_scenarios():
    print("Testing forwarded 404 (for non-existent group)...")
    status, _ = request("GET", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/devices")
    assert status == 404, f"Expected 404 for non-existent group GET list, got {status}"

    status, _ = request("POST", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/devices",
                        body={"client_mac": TOPOLOGY_KNOWN_MAC})
    assert status == 404, f"Expected 404 for non-existent group POST, got {status}"

    status, _ = request("DELETE", f"/api/v1/groups/00000000-0000-0000-0000-000000000000/devices/{TOPOLOGY_KNOWN_MAC}")
    assert status == 404, f"Expected 404 for non-existent group DELETE, got {status}"
    print("✅ Forwarded 404 scenarios passed")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print("Starting group-devices integration tests...")
    try:
        reset_db()
        test_group_device_crud_state_lifecycle()
        test_post_group_device_topology_validation()
        test_post_group_device_missing_config_raw()
        test_post_group_device_gw_failures()
        test_delete_group_device_missing_config_raw()
        test_delete_group_device_gw_failures()
        test_forwarded_404_scenarios()
        print("🎉 All group-devices integration tests passed!")
    except AssertionError as e:
        print(f"❌ TEST FAILED: {e}")
        sys.exit(1)
