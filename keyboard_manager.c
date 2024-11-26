#include <ncurses.h>
#include <signal.h>
#include <unistd.h>  

// Costanti per le dimensioni
#define BOX_HEIGHT 3
#define BOX_WIDTH 5
int term_rows, term_cols;
// Crea le finestre per la griglia
WINDOW *windows[3][3];
const char *symbols[3][3] = {
    {"\\", "^", "/"}, // Simboli per la prima riga
    {"<", "S", ">"}, // Simboli per la seconda riga
    {"/", "v", "\\"}  // Simboli per la terza riga
};
int start_x, start_y;

// Funzione per disegnare una casella
void draw_box(WINDOW *win, const char *symbol) {
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", symbol); // Stampa il simbolo al centro
    wrefresh(win);
}

void handle_key_pressed(WINDOW *win, const char *symbol) {
    // Cambia colore del simbolo temporaneamente
    wattron(win, COLOR_PAIR(2)); // Colore evidenziato
    mvwprintw(win, 1, 2, "%s", symbol);
    wrefresh(win);
    usleep(200000);
    wattroff(win, COLOR_PAIR(2)); // Rimuove il colore evidenziato
    draw_box(win, symbol); // Ripristina la casella
}

// Handler per il segnale SIGWINCH
void resize_handler(int sig, siginfo_t *info, void *context) {
    endwin();  // Termina temporaneamente ncurses
    refresh(); // Rientra in modalità ncurses
    clear();   // Pulisci lo schermo

    // Ottieni le nuove dimensioni del terminale
    getmaxyx(stdscr, term_rows, term_cols);

    // Aggiorna le dimensioni di ncurses
    resize_term(term_rows, term_cols);

    // Rinfresca la finestra principale
    clear();
    refresh();

    start_y = (term_rows - (BOX_HEIGHT * 3)) / 2;
    start_x = (term_cols - (BOX_WIDTH * 3)) / 2;

    // Inizializza le finestre
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            windows[i][j] = newwin(BOX_HEIGHT, BOX_WIDTH, start_y + i * BOX_HEIGHT, start_x + j * BOX_WIDTH);
            refresh();
            draw_box(windows[i][j], symbols[i][j]);
        }
    }

    // Messaggio in basso
    mvprintw(term_rows - 2, (term_cols - 30) / 2, "Press 'P' to close the program");
    refresh();
}

int main() {
    initscr();             // Inizializza ncurses
    start_color();         // Abilita i colori
    cbreak();              // Modalità input immediato
    noecho();              // Non mostra l'input dell'utente
    curs_set(0);           // Nasconde il cursore

    // Inizializza i colori
    init_pair(1, COLOR_WHITE, COLOR_BLACK); // Colore di default
    init_pair(2, COLOR_BLACK, COLOR_YELLOW); // Colore per evidenziare

    // Ottieni dimensioni del terminale
    getmaxyx(stdscr, term_rows, term_cols);

    // Calcola la posizione iniziale della griglia
    start_y = (term_rows - (BOX_HEIGHT * 3)) / 2;
    start_x = (term_cols - (BOX_WIDTH * 3)) / 2;

    // Inizializza le finestre
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            windows[i][j] = newwin(BOX_HEIGHT, BOX_WIDTH, start_y + i * BOX_HEIGHT, start_x + j * BOX_WIDTH);
            refresh();
            draw_box(windows[i][j], symbols[i][j]);
        }
    }

    // Messaggio in basso
    mvprintw(term_rows - 2, (term_cols - 30) / 2, "Press 'P' to close the program");
    refresh();

    // Registra l'handler per il ridimensionamento
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;        // Usa SA_SIGINFO per gestire segnali dettagliati
    sa.sa_sigaction = resize_handler; // Registra l'handler
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    int ch;
    while ((ch = getch()) != 'p' && ch != 'P') { // Esci con 'P'
        // Evidenzia la casella corrispondente
        switch (ch) {
            case 'w': case 'W': // Alto-Sinistra
                handle_key_pressed(windows[0][0], symbols[0][0]);
                break;
            case 'e': case 'E': // Alto
                handle_key_pressed(windows[0][1], symbols[0][1]);
                break;
            case 'r': case 'R': // Alto-Destra
                handle_key_pressed(windows[0][2], symbols[0][2]);
                break;
            case 's': case 'S': // Centro-Sinistra
                handle_key_pressed(windows[1][0], symbols[1][0]);
                break;
            case 'd': case 'D': // Centro
                handle_key_pressed(windows[1][1], symbols[1][1]);
                break;
            case 'f': case 'F': // Centro-Destra
                handle_key_pressed(windows[1][2], symbols[1][2]);
                break;
            case 'x': case 'X': // Basso-Sinistra
                handle_key_pressed(windows[2][0], symbols[2][0]);
                break;
            case 'c': case 'C': // Basso
                handle_key_pressed(windows[2][1], symbols[2][1]);
                break;
            case 'v': case 'V': // Basso-Destra
                handle_key_pressed(windows[2][2], symbols[2][2]);
                break;
            default:
                break;
        }
    }

    // Pulisci e termina
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            delwin(windows[i][j]);
        }
    }
    endwin();
    return 0;
}
