#include "types.h"
#include "user.h"
#include <stdio.h>
#include <unistd.h>

int x;

int main(int argc, char *argv[]) {
    pid_t pid1, pid2; 
    int y;
    
    x = atoi(argv[1]);
    y = atoi(argv[2]);

    pid1 = fork(); 
    
    if (pid1 < 0) { 
        fprintf(stderr, "Fork Failed"); 
        return 1; 
    } else if (pid1 == 0) {                        
        x *= 2;
        y *= 2;
         
        pid2 = getpid();
         
        printf("%d \n", pid1);      // A: 0   
        printf("%d \n", pid2);      // B 
        printf("%d %d \n", x, y);   // C 
    } else {                                      
        x /= 2; 
        y /= 2;
        
        pid2 = getpid(); 
        
        wait();
         
        printf("%d \n", pid1);      // D 
        printf("%d \n", pid2);      // E 
        printf("%d %d \n", x, y);   // F
    } 
    return 0; 
} 


// int
// main(int argc, char *argv[])
// {
//   int i;
//   int x = 0;

//   if(argc != 2)
//     exit();

//   for(i=1; i<atoi(argv[1]); i++)
//     x++;
  
//   printf(1, "pid(%d): x = %d\n", getpid(), x);
//   exit();
// }