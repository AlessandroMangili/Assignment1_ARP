cc  -o "main" "main.c" -lcjson
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

cc -o "drone" "drone.c" -lm
if [ $? -eq 0 ]; then
        echo "Compilazione di drone.c completata con successo"
    else
        echo "Errore durante la compilazione di drone.c"
    fi

cc -o "watchdog" "watchdog.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di watchdog.c completata con successo"
    else
        echo "Errore durante la compilazione di watchdog.c"
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

cc -o "obstacle" "obstacle.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di obstacle.c completata con successo"
    else
        echo "Errore durante la compilazione di obstacle.c"
    fi

cc -o "target" "target.c"
if [ $? -eq 0 ]; then
        echo "Compilazione di target.c completata con successo"
    else
        echo "Errore durante la compilazione di target.c"
    fi