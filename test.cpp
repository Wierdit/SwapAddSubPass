#include <iostream>

int test(int a, int b) {
return a+b;
}
int replace(int a, int b) {
return test(a, b++);
}
int main() {
int a = 2;
int b = 3;

std::cout<<test(2, 3)<<(3-2)<<replace(a, b)<<std::endl;
}
