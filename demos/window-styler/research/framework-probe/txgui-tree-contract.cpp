#include "txgui-tree-budget.h"
#include <iostream>

int main() {
    unsigned checks{}, failures{};
    auto check = [&](bool ok, const char *name) {
        ++checks;
        failures += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    };
    txtree::Budget empty;
    check(!empty.Complete(), "an absent root is not a complete original tree");
    txtree::Budget exact{3, 2, 2};
    check(exact.Enter(1, 0) && exact.Children(true, 2), "read a bounded root and its declared child count");
    check(exact.Next() && exact.Enter(2, 1) && exact.Children(true, 0), "read the first declared original leaf");
    check(exact.Next() && exact.Enter(3, 1) && exact.Children(true, 0),
          "read the final leaf exactly at the node budget");
    check(exact.nodes == 3 && exact.deepest == 1 && exact.Complete(),
          "an exact-budget fully read tree remains complete");
    check(!exact.Next() && !exact.Complete() && exact.limited == 1,
          "an unread next sibling makes a successful prefix incomplete");
    txtree::Budget depth{10, 1, 10};
    check(depth.Enter(1, 0) && depth.Enter(2, 1) && depth.Complete(), "exact-depth leaf is accepted");
    check(!depth.Enter(3, 2) && !depth.Complete() && depth.limited == 1,
          "unread descendants at the depth limit fail closed");
    txtree::Budget children;
    check(children.Enter(1, 0) && children.Children(true, children.maxChildren),
          "exact child-count budget is accepted");
    check(!children.Children(true, children.maxChildren + 1) && !children.Complete(),
          "over-budget child count is explicit");
    txtree::Budget countFailure;
    check(countFailure.Enter(1, 0) && !countFailure.Children(false, 0) && !countFailure.Complete(),
          "failed count getter is not mistaken for a leaf");
    txtree::Budget missing;
    missing.Enter(1, 0);
    missing.ChildFailure();
    check(!missing.Complete() && missing.errors == 1, "failed indexed child getter makes traversal incomplete");
    txtree::Budget cycle;
    check(cycle.Enter(1, 0) && !cycle.Enter(1, 1) && !cycle.Complete() && cycle.duplicates == 1,
          "a cycle or duplicate identity is not silently omitted");
    txtree::Budget null;
    check(!null.Enter(0, 0) && !null.Complete(), "a null child identity fails closed");
    txtree::Budget large;
    bool all = true;
    for (unsigned i = 1; i <= 700; ++i)
        all &= large.Enter(i, 1) && large.Children(true, 0);
    check(all && large.nodes == 700 && large.Complete(),
          "a complete tree larger than the historical 512 cap is accepted");
    std::cout << "checks=" << checks << "; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
