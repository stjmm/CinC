// test that we can fall through from default to other cases
// if default isn't last
int main(void) {
    int a = 5;
    switch(0) {
        default:
            a = 0;
        case 1:
            return a;
    }
    return a + 1;
}
