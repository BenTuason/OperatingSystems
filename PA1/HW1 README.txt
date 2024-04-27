Ben Tuason
COSC 3360
HW1

To run the program:
compile the program using: g++ -o main pa.cpp

then in the terminal enter:
./main instructions.txt input.txt

The program was ran and tested in VS Code WSL. 
When running the program please make sure that instructions.txt and input.txt is in the same directory.

instructions.txt should contain this format
input_var a,b,c,d,e;
internal_var p0,p1,p2;
c -> p0;
- a -> p0;
b -> p1;
/ e -> p1;
p0 -> p2;
/ d -> p2;
+ p1 -> p2
* p1 -> p2
write(a,b,c,d,e,p0,p1,p2)

and

input.txt should contain this format with commas separating them. 
9,32,64,5,8

example run:
ben123@ben:~/3360/PA1$ g++ -o main pa.cpp
ben123@ben:~/3360/PA1$ ./main instructions.txt input.txt
a = 9
b = 32
c = 64
d = 5
e = 8
p0 = 55
p1 = 4
p2 = 60

I've included sample instructions.txt and input.txt files to show the format or if the grader wants to just change the instructions or inputs slightly

Thanks!

