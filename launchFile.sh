cc  -o "main" "main.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di main.c completata con successo"
    else
        echo "Errore durante la compilazione di main.c"
    fi

cc -o "server"  "server.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di server.c completata con successo"
    else
        echo "Errore durante la compilazione di server.c"
    fi

cc -o "drone" "drone.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di drone.c completata con successo"
    else
        echo "Errore durante la compilazione di drone.c"
    fi

cc -o "watchdog" "watchdog.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di wd.c completata con successo"
    else
        echo "Errore durante la compilazione di wd.c"
    fi

cc -o "keyboard_manager" "keyboard_manager.c" "-lncurses"
if [ $? -eq 0 ]; then
        echo "Compilazione di keyboard_manager.c completata con successo"
    else
        echo "Errore durante la compilazione di keyboard_manager.c"
    fi

cc -o "map_window" "map_window.c" "-lncurses"
if [ $? -eq 0 ]; then
        echo "Compilazione di map_window.c completata con successo"
    else
        echo "Errore durante la compilazione di map_window.c"
    fi
