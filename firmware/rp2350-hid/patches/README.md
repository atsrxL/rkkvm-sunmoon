# PIO-USB 0.7.2 device adaptation

Only for commit 3c1eec341a5232640e4c00628b889b641af34b28. prepare_pio.py verifies both original source SHA256 values, copies the dependency into the build directory and uses git apply --check before applying. The shared dependency remains pristine. Preserve the upstream copyright/license headers.

The patch removes the small upstream HID-only device stack and weak callback, retaining the PIO packet engine and endpoint controller APIs. The application owns EP0 and CDC. It adds ACK-only IN progress, duplicate DATA PID filtering for OUT, application buffer bounds, validated/copied SETUP, address update after ACK, nonblocking bus reset and a SOF timestamp; unused GP3/GP4 debug writes are removed. Low-level receive bounds and zero-length pointer arithmetic are hardened. Some receive waits have a bounded timeout; this is not a claim that every electrical fault is recoverable without a reset.

The packet IRQ invokes the independent stack on core1. Foreground endpoint changes are protected from that IRQ. Cross-core sharing uses C11 atomic SPSC rings and generation acknowledgement, never endpoint structures. EP0 status does not precede OUT data validation. USB reset initializes endpoint data toggles; CLEAR_FEATURE(HALT) resets the affected toggle. A full bulk IN packet is followed by an explicit ZLP.

Host tests cover the CDC layer with an explicit controller simulation; they do not execute the PIO programs, validate bus ACK timing, measure clock accuracy or prove enumeration. Both ON/OFF cross-builds and all host tests must be repeated after a patch change; first board validation must include retries, 64-byte boundaries, hubs, suspend and DTR transitions. See ADR-012 for Chinese implementation and wiring notes.
