int main(void) {
    goto label2;
    return 0;
    // okay to have space or newline between label and colon
    label1 :
    label2
    :
    return 1;
}
