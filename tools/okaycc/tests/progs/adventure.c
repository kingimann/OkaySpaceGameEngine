// exit 0
int printf(char *fmt, ...);
int room;
int score;
void look(void) {
    if (room == 0) printf("hall\n");
    else if (room == 1) printf("cave\n");
    else printf("end\n");
}
void act(int cmd) {
    if (room == 0 && cmd == 1) {
        room = 1;
        score = score + 1;
    } else if (room == 1 && cmd == 2) {
        room = 2;
        score = score + 2;
    }
}
int main() {
    room = 0;
    score = 0;
    look();
    act(1);
    look();
    act(2);
    look();
    printf("%d\n", score);
    if (score != 3) return 1;
    return 0;
}
