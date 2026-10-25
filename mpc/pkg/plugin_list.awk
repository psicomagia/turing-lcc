# Edita a lista de plugins do MPC.settings (awk do BusyBox).
# Adaptado de tools/release/plugin_list.awk do projeto mpc-vst-plugins (https://github.com/sd88me/mpc-vst-plugins),
# licença MIT, Copyright (c) 2026 sd88me.
#   awk -v mode=add    -v file=/caminho/x.so -v uid=4c635064 -v entryfile=entrada.xml -f plugin_list.awk MPC.settings
#   awk -v mode=remove -v file=/caminho/x.so -v uid=4c635064 -f plugin_list.awk MPC.settings
function flush() {
    drop = index(buf, "file=\"" file "\"") != 0
    if (!drop && alt != "" && index(buf, "file=\"" alt "\"") != 0) drop = 1
    if (!drop && uid != "" && index(buf, " uid=\"" uid "\"") != 0) drop = 1
    if (!drop) print buf
    buf = ""
}
BEGIN {
    if (mode == "add") { while ((getline l < entryfile) > 0) entry = entry l; close(entryfile) }
    if (listkey == "") listkey = "pluginList-arm"
    done = 0; inlist = 0; buf = ""
}
buf != "" { buf = buf "\n" $0; if ($0 ~ /\/>/) flush(); next }
/<PLUGIN( |$)/ { buf = $0; if ($0 ~ /\/>/) flush(); next }
$0 ~ "<VALUE name=\"" listkey "\">" { inlist = 1; print; next }
inlist && /<KNOWNPLUGINS\/>/ {
    if (mode == "add") {
        ind = $0; sub(/<.*/, "", ind)
        print ind "<KNOWNPLUGINS>"; print ind "  " entry; print ind "</KNOWNPLUGINS>"; done = 1
    } else print
    inlist = 0; next
}
inlist && /<\/KNOWNPLUGINS>/ {
    if (mode == "add" && !done) { ind = $0; sub(/<.*/, "", ind); print ind "  " entry; done = 1 }
    inlist = 0; print; next
}
/<\/PROPERTIES>/ {
    if (mode == "add" && !done) {
        print "  <VALUE name=\"" listkey "\">"; print "    <KNOWNPLUGINS>"; print "      " entry
        print "    </KNOWNPLUGINS>"; print "  </VALUE>"; done = 1
    }
    print; next
}
{ print }
