# External-trigger snapshots: implementation and test guide

## 1. Work in the prepared copy first

Original application: `C:\Users\ROG\Documents\XQ\pressure_mat_backend`.

Prepared implementation: this guide's folder, `snapshot-implementation` in the pressure mat chat workspace. The original application and firmware were not edited. The prepared folder includes the existing dashboard plus the changes below. No Pico firmware update is needed for this version.

Open this folder's `CMakeLists.txt` in Qt Creator, using the installed **Qt 6.11.2 MinGW 64-bit** kit. Use a new build directory; do not reuse the original project's build cache.

The companion `snapshot-update.patch` contains exact edits against the local application source. To adopt it later, first preserve any work in your original repository, create a branch, then run `git apply --check <full-path-to-patch>` followed by `git apply <full-path-to-patch>`. If the check fails, compare the files instead of forcing it. New files and edited files are also packaged in `snapshot-update.zip`; extracting them over a changed repository can overwrite local edits, so testing this copy first is preferable.

## 2. Understand the existing acquisition path

This application uses a **Qt backend and a TypeScript browser dashboard**, rather than Qt visualization widgets:

1. `SerialReceiver::handleReadyRead()` reads serial bytes.
2. `PressureFrameParser::append()` validates complete 1,568-byte frames.
3. `frameReceived` goes directly to `PressureWebSocketServer::broadcastFrame()` as binary data.
4. `frontend/src/main.ts::handleFrame()` rearranges wire index `x * 28 + y` into display index `y * 56 + x`, then refreshes the live heatmap and 3D surface.

That live connection remains in place. Snapshot data uses separate JSON messages and a separate canvas.

## 3. Add the timestamped circular buffer

New file: `snapshotbuffer.h`.

- Fixed capacity: 256 complete frames, roughly 5.1 seconds at 50 Hz.
- One `std::chrono::steady_clock` timestamp reference for frames and triggers.
- Raw values stay in firmware wire order; interpolation retains double precision.
- The buffer lives on the heap to avoid putting about 3.2 MB on the Windows stack.
- Reject non-increasing frame timestamps and interpolation intervals over 100 ms.

For target time `T`, find frames `A` and `B` with `tA <= T <= tB`. For each cell:

```text
alpha = (T - tA) / (tB - tA)
value = A + alpha * (B - A)
```

An exact timestamp match returns that frame. A target outside the stored range never produces an extrapolation.

## 4. Add the asynchronous trigger service and connect it

New files: `snapshotservice.h`, `snapshotservice.cpp`.

- Bind UDP **127.0.0.1:9003** exclusively. Other computers cannot trigger it.
- Accept a datagram containing `SNAPSHOT` (surrounding whitespace is allowed).
- Timestamp when the backend processes the datagram. Do not use the sender's wall clock.
- Queue the request, return `snapshot_pending`, then return to Qt's event loop immediately.
- On each new frame, resolve requests that now have bracketing frames.
- Fail after 250 ms without a usable later frame. Also reject stale frames, missing history, intervals over 100 ms, and more than 32 pending requests.
- Process at most 64 incoming datagrams per event-loop turn, then schedule another turn if needed.
- Keep the last successful snapshot in memory. Errors and stop/disconnect leave that image available. A backend restart clears it.

Edits to existing files:

1. `serialreceiver.h`: add `timestampedFrameReceived(payload, timeNs, batched)`.
2. `serialreceiver.cpp`: timestamp at entry to `handleReadyRead()`; emit the new signal for validated frames, followed by the existing live signal. If one read parses multiple frames, mark them as batched: their individual timing is unknown. Live frames still display, while snapshot history is cleared and pending requests fail.
3. `main.cpp`: create `SnapshotService`, connect the timestamped signal, and broadcast its JSON messages. Bind UDP at startup and report a bind failure. Reset snapshot history/pending requests on acquisition start/stop, port changes, and disconnection. Replay the last successful snapshot when a browser connects.
4. `CMakeLists.txt`: select C++17; register the new files. Qt Network was already linked, so no new production Qt module is required. Tests use Qt Test.

No `waitForReadyRead`, sleep, polling loop, or worker-thread join is added to the capture path. Waiting for the next frame is asynchronous. The small per-cell interpolation runs on the existing event-loop thread; this is not a hard real-time system.

## 5. Add the frozen display

New file: `frontend/src/SnapshotView.ts`.

Edits: `frontend/src/main.ts`, `frontend/index.html`, `frontend/src/style.css`.

`main.ts` passes snapshot JSON to `SnapshotView`, while binary messages still go to `handleFrame()`. The frozen canvas changes only on a successful snapshot. It uses the same matrix orientation and color palette as the live heatmap, with a fixed raw 0–255 scale. Live noise/maximum sliders do not alter it. A red CoP marker with a white outline uses the captured fractional pressure values at threshold zero, matching this raw scale. It remains frozen with the image and is omitted for all-zero pressure.

Use **Open second-screen view** to open `http://127.0.0.1:8080/?snapshot=1`. Move that window to the second monitor. It hides the live controls and graphs and receives the backend's last successful snapshot even if opened after the trigger. The original dashboard keeps running.

**Save JSON** downloads the current snapshot, including fractional values, trigger/frame timestamps, interpolation fraction, and interval. Timestamp strings are process-clock values for relative timing, not calendar times or portable sender timestamps. Snapshots are not automatically written to disk.

## 6. Build and run the automated tests

In PowerShell, change into this guide's folder, then run:

```powershell
# Installed tools on this laptop
$env:PATH = 'C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;' + $env:PATH
$cmakeExe = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'

Push-Location frontend
npm.cmd ci
npm.cmd run build
Pop-Location

& $cmakeExe -S . -B build-snapshot -G Ninja `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64 `
    -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
& $cmakeExe --build build-snapshot --parallel 4
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build-snapshot --output-on-failure --timeout 15
node tests/frontend_snapshot_test.cjs
```

The prepared copy already has locally copied frontend dependencies, so `npm ci` is optional for the first local run. It is appropriate for a fresh checkout. Run the frontend build before the C++ build so CMake copies the current dashboard beside the executable.

Run the application from Qt Creator, or:

```powershell
.\build-snapshot\pressure_mat_backend.exe
```

It opens the dashboard as before. Close any other backend instance first: HTTP 8080, WebSocket 9002, and UDP 9003 must be free. For a production build without tests, configure with `-DBUILD_TESTING=OFF`.

Automated coverage: known interpolation values and endpoints; invalid timestamps; no extrapolation; excessive intervals; circular wraparound; real localhost UDP acknowledgments/results; malformed and oversized datagrams; inactive/stale acquisition; timeout; acquisition reset; batched-frame rejection; queue bounds; exclusive binding; simulated 50 Hz acquisition continuing through six captures. Frontend logic checks cover second-screen mode, validation, wire orientation, frozen retention, and full-precision JSON export.

## 7. Test with the physical mat

1. Connect the mat in the dashboard and click **Start**. Confirm the live FPS/frame counter advances and both graphs respond.
2. Open the second-screen view and move it to the other monitor.
3. In a separate PowerShell window, change into this guide's folder and run:

   ```powershell
   python tools/send_snapshot.py
   ```

4. Expect a queued message followed by a successful capture. Both snapshot views should match. Shift your weight: the live graph and counter must continue changing, while the snapshot stays fixed.
5. Trigger again. Only then should the frozen image update. Open a fresh second-screen tab and confirm it receives the same last snapshot.
6. Click **Save JSON**. Check `values` has 1,568 entries, `beforeNs <= triggerNs <= afterNs`, `alpha` lies in [0,1], and `gapMs <= 100`.
7. Click **Stop**, then trigger. Expect a capture error and retention of the previous image. Restart acquisition, wait for frames, and trigger successfully again.
8. Disconnect/reconnect the mat and repeat. A pending capture must not combine frames from different acquisition sessions.
9. Compare the live FPS before and during a normal sequence of triggers. Record practical latency with the real piezo sender.

The sender uses only Python's standard library. Your piezo-reading program can use the same `sendto(b"SNAPSHOT", ("127.0.0.1", 9003))` call when it detects a strike. UDP can lose packets; the provided sender waits for a result rather than claiming that sending alone proves capture. It does not automatically retry, because a retry would select a later instant.

## Timing limits to retain in the product description

This version estimates pressure at **backend trigger processing time** from **host frame receipt times**. It does not recover the exact physical strike instant. USB buffering, scheduling, piezo detection delay, and row-by-row scanning contribute timing error. A frame is a sequential scan, not simultaneous measurements of every cell. Linear interpolation estimates intermediate ADC values; it cannot reconstruct an unsampled impact peak or establish calibrated force.

For better physical synchronization, the next stage is acquisition timestamps/frame counters from the Pico plus a timestamped piezo event on a verified common clock. Measure/calibrate the complete timing path before claiming precise impact alignment.

The automated tests use synthetic pressure values. Physical mat behavior, piezo integration, visual layout in a running browser, and dual-monitor operation still require the manual checks above.
