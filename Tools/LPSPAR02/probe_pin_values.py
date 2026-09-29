import unreal

# READ ONLY. Pin default values (get_pin_value / export_text), which is how the comparison thresholds
# can actually be read: these are K2Node pins, not anim node properties.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
SAVED = unreal.Paths.project_saved_dir() + 'LPSP_AR02/'
L = []


def log(m):
    L.append(str(m))


pkg = unreal.load_asset(AB).get_outermost()


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        try:
            if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
                return n
        except Exception:
            pass
    return None


for nm in ('K2Node_CallFunction_7', 'K2Node_CallFunction_4', 'K2Node_CallFunction_3', 'K2Node_CallFunction_5',
           'K2Node_VariableGet_7', 'K2Node_VariableGet_8'):
    n = find(nm)
    log('')
    if not n:
        log('%s MISSING' % nm)
        continue
    try:
        log('%s [%s]' % (nm, str(n.get_node_title()).replace('\n', ' ')[:70]))
    except Exception:
        log('%s' % nm)
    try:
        log('   export_text: %s' % str(n.export_text())[:300])
    except Exception as e:
        log('   export_text ERR %s' % str(e)[:80])
    for p in n.list_all_pins():
        name = str(p.get_pin_name())
        try:
            raw = p.get_pin_value()
        except Exception as e:
            raw = 'ERR %s' % str(e)[:40]
        try:
            txt = p.export_text()
        except Exception:
            txt = ''
        try:
            conn = [x.get_owning_node().get_name() for x in p.list_connected_pins()]
        except Exception:
            conn = []
        log('   %-14s value=%-22s conn=%-30s export=%s' % (name, str(raw)[:22], str(conn)[:30], str(txt)[:60]))

open(SAVED + 'pin_values.txt', 'w', encoding='utf-8').write('\n'.join(L))
print('WROTE pin_values.txt lines=%d' % len(L))
