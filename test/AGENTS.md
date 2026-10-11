# Test guidance

## Purpose

- Add regression tests for meaningful firmware behavior, not to maximize test count.
- Read the production code, its callers, and relevant invariants before writing a test. Assert externally visible results rather than restating implementation details.
- Cover important boundaries and failure paths where practical. Do not encode known-bad behavior as the expected result.
- Treat generated tests as unreviewed until their assumptions and assertions have been checked against the implementation and intended behavior.

## Structure and style

- Use the existing PlatformIO native environment and Unity. Run the suite with `pio test -e native`.
- Put each component's tests in `test/<component>/test_main.cpp`; name cases `test_<behavior>`.
- Keep fixtures and shared fake state in `test_support.*`. Reset fake time and mutable state in `setUp()` so tests do not depend on execution order.
- Keep tests small: arrange inputs, call the production API, and assert the resulting state or interaction.

## Fakes and hardware boundaries

- Native tests are appropriate for deterministic logic that can be separated from hardware I/O. They do not prove electrical behavior, peripheral timing, or the correctness of a hardware library.
- Keep fakes at the dependency boundary and implement only the API the production code uses. Make them deterministic and observable; allow failures to be simulated when failure handling matters.
- Do not silently make every mock operation succeed when testing retry, shutdown, or error behavior. Avoid reproducing large parts of Arduino or peripheral libraries in test code.
- Keep mocks under `test/mocks` and scoped to the native environment. Never let them replace production headers or alter embedded-build behavior.

## Build and verification

- The native environment uses an explicit `build_src_filter`. Add a production `.cpp` file there when a new native suite needs to link it; do not compile hardware-dependent source just to broaden the filter.
- Run `pio test -e native` and `pio run` after test or test-configuration changes. Report any hardware-only validation separately; native tests cannot replace it.
