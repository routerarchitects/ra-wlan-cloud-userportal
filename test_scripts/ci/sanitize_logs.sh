#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
# Copyright (c) 2025 Infernet Systems Pvt Ltd
set -euo pipefail

# Sanitizes sensitive patterns from log streams or files before retaining CI artifacts.
# Reads from file argument if provided, otherwise standard input.
#
# Threat Model & Supported Credential Formats:
# 1. Authorization headers: Matches standard HTTP authentication schemes
#    (Bearer, Basic, Token) tied to [Proxy-]Authorization headers with case-insensitivity
#    (bearer/Bearer), colon or equals (:=), and flexible whitespace (authorization:Bearer,
#    Authorization = Bearer). Preserves trailing diagnostic context (e.g. status codes)
#    and non-credential logs mentioning 'Basic' or 'Bearer'. Raw tokens without an auth
#    scheme are not emitted by UserPortal (which enforces OAuth2 Bearer tokens) or fake services.
# 2. Cookie headers: Redacts entire value of HTTP Cookie: and Set-Cookie: headers to end of line,
#    including quoted, unquoted, and semicolon-delimited cookie values.
# 3. URI query parameters: Matches sensitive parameters (?token=, &password=, &apiKey=,
#    ?access_token=, &refresh_token=, &cookie=, etc.).
# 4. Quoted credentials: Matches sensitive keys ("password", "token", "secret",
#    "key", "apiKey", "cookie") with quoted string values, supporting both JSON/Python dict syntax
#    ("key": "value", 'key': 'value') and quoted environment/property assignments
#    (KEY="value", key='value', property = "value"). Non-string literals (null, booleans,
#    numbers) are preserved without modification.
# 5. Unquoted property & environment assignments: Matches unquoted assignments
#    (KEY=value, key: value, KEY : value, access_token=...) with boundary matching to
#    avoid over-redacting safe words (e.g. turnkey, monkey).

if [ $# -ge 1 ] && [ "$1" != "-" ]; then
  INPUT="$1"
else
  INPUT="/dev/stdin"
fi

sed -E \
  -e 's/((^|[^a-zA-Z0-9])["'\''"]?([a-zA-Z0-9_.-]*[._-])?authorization["'\''"]?[[:space:]]*[:=][[:space:]]*["'\''"]?(Bearer|Basic|Token)[[:space:]]+)[^"'\''\r\n[:space:],;]+/\1[REDACTED]/gI' \
  -e 's/((^|[^a-zA-Z0-9_'\''"-])(set-)?cookie[[:space:]]*:[[:space:]]*)[^\r\n]+/\1[REDACTED]/gI' \
  -e 's/([?&]([a-zA-Z0-9_.-]*[._-])?(token|password|secret|key|apiKey|cookie)=)[^&[:space:]"'\''`]+/\1[REDACTED]/gI' \
  -e 's/((^|[^a-zA-Z0-9])["'\''"]?([a-zA-Z0-9_.-]*[._-])?(password|secret|token|key|apiKey|cookie)["'\''"]?[[:space:]]*[:=][[:space:]]*(["'\''"]))[^"'\''"]*(["'\''"])/\1[REDACTED]\5/gI' \
  -e 's/((^|[^a-zA-Z0-9])([a-zA-Z0-9_.-]*[._-])?(password|secret|token|key|apiKey|cookie)[[:space:]]*[:=][[:space:]]*)[^"'\''[:space:],;&]+/\1[REDACTED]/gI' \
  "$INPUT"
