#include <ncurses.h>
#include <signal.h> // Per gestire i segnali come SIGWINCH
#include <stdlib.h>

int term_rows, term_cols;

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

    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0); // Disegna un bordo lungo lo schermo
    mvprintw(0, 1, "Dimension of the window: %d x %d", term_rows, term_cols);
    attroff(COLOR_PAIR(1));
    refresh();
}

int main() {
    initscr();            // Inizializza ncurses
    start_color();        // Abilita i colori
    cbreak();             // Disattiva il line buffering
    noecho();             // Non mostrare input utente
    curs_set(0);          // Nasconde il cursore

    // Definizione colori
    init_pair(1, COLOR_BLUE, COLOR_BLACK);      // Linee e cornice blu
    init_pair(2, COLOR_GREEN, COLOR_BLACK);     // Numeri verdi
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);    // Punti arancioni
    init_pair(4, COLOR_CYAN, COLOR_BLACK);      // Freccia azzurra

    getmaxyx(stdscr, term_rows, term_cols);

    // Disegna i bordi iniziali
    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0); // Disegna un bordo lungo lo schermo
    mvprintw(0, 1, "Dimension of the window: %d x %d", term_rows, term_cols);
    attroff(COLOR_PAIR(1));
    refresh();

    // Registra l'handler per il ridimensionamento
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;        // Usa SA_SIGINFO per gestire segnali dettagliati
    sa.sa_sigaction = resize_handler; // Registra l'handler
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    // Attendi input per uscire
    while(true) {}
    endwin(); // Termina ncurses

    return 0;
}
