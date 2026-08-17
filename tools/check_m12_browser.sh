#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
playwright_cli="${PWCLI:-/Users/floreacalin/.codex/skills/playwright/scripts/playwright_cli.sh}"

if [[ ! -x "$playwright_cli" ]]; then
    echo "set PWCLI to the Playwright CLI or Codex Playwright wrapper" >&2
    exit 1
fi

fixture_directory="$(mktemp -d)"
fixture_ready="$fixture_directory/ready"
python3 "$repository_root/tools/m12_browser_fixture.py" \
    --port 0 --ready-file "$fixture_ready" &
fixture_pid=$!
session="smoker-m12-$fixture_pid"

cleanup() {
    "$playwright_cli" --session "$session" close >/dev/null 2>&1 || true
    kill "$fixture_pid" >/dev/null 2>&1 || true
    wait "$fixture_pid" >/dev/null 2>&1 || true
    rm -rf "$fixture_directory"
}
trap cleanup EXIT

for _ in {1..50}; do
    if ! kill -0 "$fixture_pid" >/dev/null 2>&1; then
        echo "M12 browser fixture exited before readiness" >&2
        wait "$fixture_pid"
        exit 1
    fi
    [[ -s "$fixture_ready" ]] && break
    sleep 0.1
done
[[ -s "$fixture_ready" ]] || { echo "M12 browser fixture did not publish ports" >&2; exit 1; }

commissioning_port="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commissioning"])' "$fixture_ready")"
operational_port="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["operational"])' "$fixture_ready")"
commissioning_base="http://127.0.0.1:$commissioning_port"
operational_base="http://127.0.0.1:$operational_port"

# The Playwright skill requires a fresh snapshot before element references or interaction.
"$playwright_cli" --session "$session" open "$commissioning_base/" >/dev/null
"$playwright_cli" --session "$session" snapshot >/dev/null
browser_output="$fixture_directory/browser-output"
if ! "$playwright_cli" --session "$session" run-code "async (page) => {
    const browserErrors = [];
    page.on('console', entry => {
        if (entry.type() === 'error') browserErrors.push(entry.text());
    });
    const apBase = '$commissioning_base';
    const staBase = '$operational_base';

    if (await page.title() !== 'Fumuri · configurare Wi-Fi') {
        throw new Error('commissioning title missing');
    }
    if (!await page.getByText('NUMAI WI-FI').count()) throw new Error('AP scope label missing');
    if (await page.locator('#dashboard, #chamber, #heater, #login-password').count()) {
        throw new Error('AP exposes operational/login UI');
    }
    await page.locator('.network:disabled').first().waitFor();
    const unsupportedSetup = page.locator('.network:disabled').first();
    if (!await unsupportedSetup.isDisabled()) throw new Error('AP unsupported network selectable');
    await page.locator('#ssid').fill('Atelier');
    await page.locator('#wifi-password').fill('parola-wifi');
    await page.getByRole('button', {name: 'Salvează și conectează'}).click();
    await page.locator('#message').filter({hasText: 'Configurația a fost salvată'}).waitFor();

    const apPage = page;
    page = await page.context().newPage();
    page.on('console', entry => {
        if (entry.type() === 'error') browserErrors.push(entry.text());
    });
    await page.goto(staBase + '/login');
    if (await page.title() !== 'Fumuri · autentificare') throw new Error('login title missing');
    if (await page.locator('input[name=username]').count()) throw new Error('login requested username');
    const password = page.locator('#login-password');
    if (await password.evaluate(element => getComputedStyle(element).fontSize) !== '16px') {
        throw new Error('login input can trigger iPhone zoom');
    }
    const toggle = page.locator('#toggle-password');
    await toggle.click();
    if (await password.getAttribute('type') !== 'text') throw new Error('password reveal failed');
    await page.getByRole('button', {name: 'Ascunde'}).click();
    await password.fill('smoker257500');
    await page.getByRole('button', {name: 'Intră în Fumuri'}).click();
    await page.waitForTimeout(500);
    if (page.url() !== staBase + '/') {
        throw new Error('STA login did not reach dashboard: ' + page.url() + ' body=' + await page.locator('body').innerText());
    }
    const cookies = await page.context().cookies();
    const sessionCookie = cookies.find(cookie => cookie.name === 'smoker_session');
    if (!sessionCookie || !sessionCookie.httpOnly || sessionCookie.sameSite !== 'Lax') {
        throw new Error('session cookie flags missing');
    }

    // A valid STA cookie is deliberately sent to the same host's AP port; scope wins.
    apPage.removeAllListeners('console');
    const blockedResponse = await apPage.goto(apBase + '/api/v1/snapshot');
    if (!blockedResponse || blockedResponse.status() !== 403) {
        throw new Error('valid cookie unlocked snapshot through AP');
    }
    browserErrors.length = 0;
    if (!await page.locator('#firmware-panel').count()) throw new Error('firmware panel missing');
    await page.setViewportSize({width: 390, height: 844});
    const viewportOverflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
    if (viewportOverflow) throw new Error('firmware dashboard overflows iPhone-width viewport');
    if (await page.locator('#firmware-check').evaluate(button => button.getBoundingClientRect().height) < 44) {
        throw new Error('firmware check touch target is smaller than 44px');
    }
    await page.setViewportSize({width: 1280, height: 900});
    await page.locator('#firmware-current').filter({hasText: '0.13.0'}).waitFor();
    await page.locator('#firmware-check').click();
    await page.locator('#firmware-available').filter({hasText: '0.13.1'}).waitFor();
    await page.locator('#firmware-install').click();
    await page.waitForFunction(() => document.querySelector('#firmware-progress').style.width === '42%');
    if (!await page.locator('#start').isDisabled()) {
        throw new Error('Start remains enabled while firmware installation is active');
    }
    await page.locator('#firmware-error').filter({hasText: 'fixture_download_failed'}).waitFor({timeout: 7000});
    await page.waitForFunction(() => !document.querySelector('#start').disabled);
    const active = page.locator('#active-target');
    await page.locator('[data-probe-target=\"1\"]').waitFor();
    await active.fill('88.5');
    await page.waitForTimeout(2200);
    if (await active.inputValue() !== '88.5') throw new Error('active target draft lost');
    const probe = page.locator('[data-probe-target=\"1\"]');
    const readingBefore = await page.locator('[data-probe-row=\"1\"] .probe-reading').textContent();
    await probe.fill('66.5');
    await page.waitForTimeout(3200);
    if (await probe.inputValue() !== '66.5') throw new Error('probe target draft lost');
    const readingAfter = await page.locator('[data-probe-row=\"1\"] .probe-reading').textContent();
    if (readingAfter === readingBefore) throw new Error('focused probe froze live reading');
    const unsupported = page.locator('.network-option.unsupported').first();
    await unsupported.waitFor();
    if (!await unsupported.isDisabled()) throw new Error('unsupported STA network selectable');
    if (!await page.locator('#password-form').count() || !await page.locator('#logout').count()) {
        throw new Error('password change/logout controls missing');
    }
    await page.locator('#chamber-form button').click();
    await page.locator('#message').filter({hasText: 'Controlerul a respins'}).waitFor({timeout: 7000});
    await page.waitForFunction(() => document.querySelector('#active-target').value === '');
    await page.locator('[data-probe-apply=\"1\"]').click();
    await page.locator('#message').filter({hasText: 'Controlerul a respins'}).waitFor({timeout: 7000});
    await page.waitForFunction(() => document.querySelector('[data-probe-target=\"1\"]').value === '63');
    await page.locator('#start').click();
    await page.locator('#message').filter({hasText: 'Comanda a fost aplicată'}).waitFor({timeout: 7000});
    const duplicateStartAccepted = await page.evaluate(() => command({
        type: 'start_session', target_celsius: 110
    }));
    if (duplicateStartAccepted) throw new Error('duplicate Start was semantically accepted');
    await page.locator('#message').filter({hasText: 'Controlerul a respins'}).waitFor({timeout: 7000});
    await page.waitForTimeout(3000);
    const metrics = await page.evaluate(async () => (await fetch('/fixture/metrics')).json());
    if (metrics.maximum_snapshot_requests !== 1) {
        throw new Error('snapshot polling overlapped: ' + metrics.maximum_snapshot_requests);
    }
    const external = await page.evaluate(() => performance.getEntriesByType('resource')
        .map(entry => new URL(entry.name, location.href))
        .filter(url => url.origin !== location.origin && url.protocol !== 'data:')
        .map(url => url.href));
    if (external.length) throw new Error('external resources loaded: ' + external.join(', '));
    if (browserErrors.length) throw new Error('browser console errors: ' + browserErrors.join(' | '));
}" >"$browser_output" 2>&1; then
    cat "$browser_output" >&2
    exit 1
fi

echo "M12/M13 AP, STA session, and firmware browser contract: PASS"
