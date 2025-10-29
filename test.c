#include <stdio.h>

typedef enum {A, B, C} Letter;

int main(){
    Letter l;
    char a = 'A';
    l = a >= 'A' && a <= 'Z' ? (Letter)a - 'A' : (Letter)a - 'a';
    printf("%d\n", l);
    return 0;
}