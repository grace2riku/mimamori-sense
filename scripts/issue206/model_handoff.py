"""Bounded reasoning model, NOT a hardware/FSP emulator or production test.

Assumptions: CPU stores become visible in program order; no pending DTC work
survives the completed disable/quiesce boundary; no unrelated writer modifies
the descriptor. These assumptions are deliberately NOT established by this
model. IRQ RMW and terminal completion are modelled at instruction/event level.
"""
import itertools
import json


def irq_rmw_cases():
    cases = []
    for order in itertools.permutations(("cpu_read", "cpu_store", "dtc_end")):
        if order.index("cpu_read") > order.index("cpu_store"):
            continue
        enabled, saved = True, None
        for event in order:
            if event == "cpu_read":
                saved = enabled
            elif event == "cpu_store":
                enabled = saved  # clearing IR preserves sampled DTCE
            else:
                enabled = False  # normal terminal hardware auto-disable
        cases.append({"order": order, "terminal_reenabled": enabled})
    return cases


def publication_cases():
    # Starting point: application completion guard has passed, DTCE=0,
    # CRB=0, old transfer fully written back. Insert one request at every
    # boundary of reset. No arbitrary count corruption is injected.
    stores = ("disable", "quiesce", "rrs0", "source", "count", "rrs1", "enable")
    cases = []
    for pos in range(len(stores) + 1):
        enabled, source, count = False, "old_end", 0
        observed = None
        events = list(stores)
        events.insert(pos, "request")
        for event in events:
            if event == "disable":
                enabled = False
            elif event == "source":
                source = "new_start"
            elif event == "count":
                count = 162
            elif event == "enable":
                enabled = True
            elif event == "request" and enabled:
                observed = {"source": source, "blocks": count or 65536}
        cases.append({"request_boundary": pos, "accepted": observed})
    return cases


if __name__ == "__main__":
    rmw = irq_rmw_cases()
    publication = publication_cases()
    assert [c["order"] for c in rmw if c["terminal_reenabled"]] == [
        ("cpu_read", "dtc_end", "cpu_store")]
    assert all(c["accepted"] is None or c["accepted"] ==
               {"source": "new_start", "blocks": 162} for c in publication)
    print(json.dumps({
        "scope": "bounded abstract interleavings; not evidence of real bus ordering",
        "irq_rmw": rmw,
        "reset_publication": publication,
        "observed_bytes": 0x40000,
        "normal_then_extra_zero_count_bytes": 162 * 4 + 65536 * 4,
        "zero_count_only_cycles_at_1GHz": 65536 * 768 * 80,
        "recording_log_cycles": 4026531378,
        "conclusion": "RMW has a conditional witness, but alone predicts the wrong address delta. "
                      "Ordered/quiescent publication does not yield a zero-count activation. "
                      "No conclusion about violation of the stated hardware assumptions."
    }, indent=2))
