#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define size 8

#define RST_GLBL o=bckgr=type_gl=flg_gl=0; //reseting globals to origin
    
jmp_buf begin;

// global variables for building tree
int o = 0; //offset for str
int bckgr = 0; //tree background, for conveyer
int type_gl = 0; //tree type, for conveyer
int flg_gl = 0; //point of return, for conveyer

volatile int bg_ps = 0; //number of background ps

typedef struct cmd_inf tree;

typedef struct cmd_inf {
    char ** argv; // список из имени команды и аргументов
    char *infile; // переназначенный файл стандартного ввода
    char *outfile; // переназначенный файл стандартного вывода
    char *apinfile; // переназначенный файл <<
    char *apoutfile; // переназначенный файл >>
    int backgrnd; // =1, если команда подлежит выполнению в фоновом режиме
    int type; //'&&'-1  '||'-2
    tree* psub; // команды для запуска в дочернем shell
    tree* pipe; // следующая команда после “|”
    tree* next; // следующая после “;” (или после “&”)
}cmd_inf;


void print_tree (tree* q, tree* p) //print tree recursively
{
   if(q){
    int i = 0;
    printf("begin of structure \n--------------\n");
    printf("args: ");
    if (q->argv)
    for(i = 0; q->argv[i]; ++i)
        printf("%s ",q->argv[i]);
    printf("(%d)\n",i);
    printf("file:\n");
    printf("in = %d\n", (int)(q->infile ? 1:0));
    printf("out = %d\n",(int)(q->outfile ? 1:0));
    printf("apin = %d\n",(int)(q->apinfile ? 1:0));
    printf("apout = %d\n",(int)(q->apoutfile ? 1:0));
    printf("bckgrnd = %d\n\n",q->backgrnd);
    printf("type = %d\n\n",q->type);
    printf("psub = %ld\n",(long) q->psub);
    printf("pipe = %ld\n",(long)q->pipe);
    printf("next = %ld\n",(long)q->next);
    printf("-------------- \nend of structure\n");
   }
   else printf("print_struct ptr = NULL\n");

    
    char buf[10];
    fgets(buf,10,stdin);
    switch(buf[1]){
        case 'e': print_tree(q->next,q); break;
        case 'i': print_tree(q->pipe,q); break;
        case 's': print_tree(q->psub,q); break;
        case 'a': print_tree(p,q); break;
        default: printf("------------\nend of print\n");
    }   
}

tree* build_tree(char** s)//building tree of cmd
{
    if (s==NULL) return NULL;
    tree* q = (tree*) calloc ( sizeof(cmd_inf),sizeof(char) ); //allocate struct cmd_inf
    if (!q) { fprintf(stderr,"tree: tree calloc err\n"); return q; }
    int ex = 1; // exit after return
    int i;
    char** tq = NULL;
    int flg_r = 0; //return flag
    for(i = 0; ex && s[o]; ++o,++i){
        switch( *s[o] ){
         case '>': if (*(s[o]+1)) 
                       q->apoutfile = s[o+1];
                   else q->outfile = s[o+1]; break; 
         case '<': if (*(s[i]+1))
                       q->apinfile = s[o+1];
                   else q->infile = s[o+1]; break;
         case '&': if (*(s[o]+1)) 
                   {
                       q->type = 1;
                       type_gl = 1;
                   }
                   else
                   { 
                       q->backgrnd = 1;  
                       bckgr = 1;
                   }
                   if(flg_gl) 
                      return q;
                   q->next  = build_tree(s+1); ex = 0; break;
         case '|': if (*(s[o]+1))
                   {
                       q->type = 2;
                       type_gl = 2;
                   }
                   else
                   {
                       if(!flg_gl){ //return here //smfr
                           flg_gl = 1;
                           flg_r = 1;
                       }
                       q->pipe = build_tree(s+1); 
                       q->backgrnd = bckgr; //copy backgrnd atributes from last cmd of conveyer
                       q->type = type_gl; //copy type -- \\ --
                       if(!flg_r){ 
                           flg_gl++; //num of returns. to offset
                           return q;
                       }else{ //unblock
                           o += flg_gl; // ptr on ; or &
                           bckgr = type_gl = flg_gl = 0;
                           flg_r = 0;
                       }
                       if ( s[o] == NULL) break;
                   }
                   if(flg_gl)
                       return q;
                   q->next = build_tree(s+1); ex = 0; break; //if '||'
         case ';': if(flg_gl) 
                      return q;
                   q->next = build_tree(s+1); ex = 0; break;
         default:
                   tq = (char**) realloc(q->argv,(2+i)*sizeof(char*));
                   if(tq) q->argv = tq; 
                   else { fprintf(stderr,"tree: argv realloc err\n"); return q; }
                   q->argv[i] = s[o];
                   q->argv[i+1] = NULL; 
        } 
    }
    return q;
}

int flow_file(tree* t) //transfer flow <> file
{
    int fd = 0;
    if (t->outfile) //output > or >>
    {
        fd = open(t->outfile,O_WRONLY|O_CREAT|O_TRUNC,0666);
        if (fd == -1){
        perror("write file error");
        return -1;
        }
        dup2(fd,1);
        close(fd);
    }
    else if (t->apoutfile)
    {
        fd = open(t->apoutfile,O_WRONLY|O_CREAT|O_APPEND,0666);
        if (fd == -1){
        perror("write file error");
        return -1;
        }
        dup2(fd,1);
        close(fd);
    }
    if (t->infile) //input <
    {
        fd = open(t->infile,O_RDONLY|O_EXCL);
        if (fd == -1){
            perror("read file error");
            return -1;
        }
        dup2(fd,0);
        close(fd);
    }

    return fd;
}

void exec_tree(tree* t) //execute cmds
{if (t){ // check tree on NULL
    pid_t ppid,pid,pid1,pid2,pid3;
    int st;
    int fd = 0;
    int pd[2],pd2[2];
    if ( (ppid = fork()) == 0 ){
    if ( t->pipe ) //conveyer
    {
        if ( (pid = fork()) == 0 )  //grand child
        {
            if (pipe(pd) < 0) { perror("pipe"); exit(1); }
 
            if ( (pid1 = fork()) == 0 ) //stdout -> 
            {    //grand grand child
                fd = flow_file(t); // <>
                if ( fd < 0 ) exit(0); //error in open()
                if ( !t->backgrnd ) 
                    signal(SIGINT,SIG_DFL);
                if (t->backgrnd)
                {
                    int nd = open ("/dev/null",O_RDONLY);
                    ++bg_ps; 
                    dup2(nd,0);
                }
                dup2(pd[1], 1);
                close( pd[0] );
                close( pd[1] );
                execvp( *t->argv, t->argv);
                perror("exec error");                
                exit(1);
            }
            else if (pid1 < 0) { perror("fork"); exit(1); }

            t = t->pipe;
            int pic = 0;
            while( t->pipe )
            {      
                ++pic;
                if (pic % 2) pipe(pd2); //mod = 1
                else pipe(pd);
                if ( (pid2 = fork()) == 0) //stdout -> stdin
                { 
                    if ( !t->backgrnd ) 
                        signal(SIGINT,SIG_DFL);
                    switch(pic % 2){
                    case 0: //pd2->fd
                            dup2(pd2[0], 0);
                            dup2(pd[1], 1);
                            break;
                    case 1: //pd->pd2
                            dup2(pd[0], 0);
                            dup2(pd2[1], 1);
                    }
                    close( pd[0] ); close ( pd2[0] ); //close child's descr
                    close( pd[1] ); close ( pd2[1] );
                    execvp( *t->argv, t->argv);
                    perror("exec error");
                    exit(1);
                }
                else if (pid2 < 0) { perror("fork"); exit(1); }
                
                if (pic % 2){ close( pd[0] ); close( pd[1] ); }
                else{ close ( pd2[0] );  close ( pd2[1] ); }
                t = t->pipe;
            }
            
            if ( (pid3 = fork()) == 0 ) // -> stdin
            {   
                if ( !t->backgrnd ) 
                    signal(SIGINT,SIG_DFL);
                switch(pic % 2){
                case 0:
                        dup2(pd[0], 0);
                close( pd[1] ); close ( pd[0] );
                        break;
                case 1:
                        dup2(pd2[0], 0);
                close( pd2[1] ); close ( pd2[0] ); //close child's descr
                }
                fd = flow_file(t); // <>
                if ( fd < 0 ) exit(0); //error in open()
                execvp( *t->argv, t->argv);
                perror("exec error");                
                exit(1);
            }
            else if (pid3 < 0) { perror("fork"); exit(1); }
            else if ( !t->backgrnd ){ // check ps background mode 
                    close( pd[0] ); close ( pd2[0] ); //close child's descr
                    close( pd[1] ); close ( pd2[1] );
                    waitpid(pid1,&st,0); //wait for grand grand childs 
                    waitpid(pid2,&st,0);
                    waitpid(pid3,&st,0); 
            }
            close( pd[0] ); close ( pd2[0] ); //close child's descr
            close( pd[1] ); close ( pd2[1] );
            exit(0); //close grand grand child ps
        }
        else if ( !t->backgrnd ) // check ps background mode 
                 waitpid(pid,&st,0); //wait for grand child 
        
        exit(0);//close child
    
    } //end of conveyer
    else // no conveyer 
    if ( (pid = fork()) == 0 ) //grand child
    {    
        if ( !t->backgrnd ) 
              signal(SIGINT,SIG_DFL);
        if (t->backgrnd)
        {
              int nd = open ("/dev/null",O_RDONLY);
              ++bg_ps; 
              dup2(nd,0);
        }
        fd = flow_file(t); // <>
        if ( fd < 0 ) exit(0); //error in open()
        execvp( *t->argv, t->argv );
        perror("exec error");
        exit(1);
    }
    else if (pid < 0) { perror("fork"); exit(1); }
    else if ( !t->backgrnd )
            waitpid(pid,&st,0);
    exit(0); //close child
    }else waitpid(ppid,&st,0);

    if( t->type )
    {   //check on '||' and '&&'
        if( WIFEXITED( st ) && WEXITSTATUS( st ) == 0 ) 
        {
            if (t->type == 1) // ~ '&&'
                 exec_tree( t->next );
            else return ; //type == 2 ~ '||'
        }
        else //prev not ended
        {
            if (t->type == 2) // ~ '||'
                 exec_tree( t->next );
            else return ; //type == 1 ~ '&&'
        }
    }
    else if (t->next) exec_tree( t->next );

}}

int check_sym (char sym) //allowing checking symbols
{
    char allow[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567890._$/:\"\'";
    int j;
    for (j = 0; allow[j] != '\0'; ++j)
        if (allow[j] == sym) return 1;
    return 0;
}



void error_list (char c) //syntax mistake
{
    fprintf(stderr,"bash: syntax error near unexpected token '%c'\n",c);
    longjmp(begin,1);
}

int check_list (char **s) //check on syntax mistakes
{
    if(*s==NULL) return 0;
    int i;
    int slash = 0;
    int cmd = 1; // cmd requires
    for (i = 0; s[i]; ++i) 
    {
        if (*s[i] != '\\' && s[i+1] == NULL) slash = 1;
        if ( cmd && check_sym(*s[i]) ) cmd = 0;
        else if (cmd) error_list( *s[i] );
        switch ( *s[i] ){
         case ';':case '|':
         case '>':case '<': 
         cmd = 1; break;
         case '&': 
            if( *(s[i]+1) || s[i+1] ) cmd = 1;
        }
    }
    if ( !slash )
    if ( cmd && (*s[i-1] != ';') ) error_list(*s[i-1]);
    
    return 1;
}

char* get_env (char** str) //get environment
{
    char* env = NULL;
    char* buf = NULL;
    int env_s = 5;
    if ( !strncmp(*str, "$HOME", env_s) ){
        buf = getenv("HOME");
        env = (char*) malloc ( strlen(buf)*sizeof(char) );
        strcpy(env,buf);
    }else
    if ( !strncmp(*str, "$USER", env_s) ){
        buf = getenv("USER");
        env = (char*) malloc ( strlen(buf)*sizeof(char) );
        strcpy(env,buf);
    }else
    if ( !strncmp(*str, "$EUID", env_s) ){
        env = (char*) malloc (10*sizeof(char) );
        sprintf(env,"%d",geteuid() ); //int -> str
    }else
    if( !strncmp(*str, "$SHELL", ++env_s) ){
        buf = getenv("SHELL");
        env = (char*) malloc ( strlen(buf)*sizeof(char) );
        strcpy(env,buf);
    }
    *str += env_s;
    
    return env;
}

char* add_str(int num, char c) //num of sym, sym | forming string with spec symbol
{
    char* str = malloc (num+1);
    if (str == NULL) {fprintf(stderr, "add_str: malloc err\n");exit(1);}
    memset(str,c,num);
    str[num]='\0'; 
    return str; //new line
}

char** form_list (char* str) //forming list from string
{
    if (str==NULL) return NULL;
    char** res = NULL;
    char* wd = NULL; //word
    int wdsize = 1; //current word size
    char* env_str; //$ENV
    int quo = 0; //quotes
    char c; //cur sym
    int next = 1; //to the next word
    int i,k;  //res[i] wd[k]
    i = k = 0;
    res = (char** ) calloc (strlen(str)*size,1);
    if (res == NULL) {fprintf(stderr,"form_list: res malloc err\n"); exit(1); }
    while ( *str )
    {
       c = *str;//current symbol
       if (quo == 0){
       switch (*str){//|, ||, &, &&, ; , >, >>, <, (, ) - spec
        case ' ':case '\t':case '\n': next = 1; //replace 'space' by 'eol'
        while(*str==' '||*str=='\n'||*str=='\t') ++str;   //skip all spaces
        continue;
        case ';':case ')':case '(':case '<':
        case '|':case '&':case '>': next = 1;
            if ( *++str == c )
             { res[i++] = add_str(2,c); str++; }
            else res[i++] = add_str(1,c);
        continue;
        case '#':
        while (*str) ++str;
        continue;
        //default:  check_sym(*str); //comment to deactivate
       }}

       if (next && c!='\'' && c!='\"')
       {
           res[i++] = wd = (char *) calloc (sizeof(char),2);
           wdsize = 1;
           k = next = 0;
       }
        
       if (c == '\'' || c == '\"')
       {    
            next = 1; ++str;
            quo = ( quo == 1 ) ? 0 : 1; //wait for next quo
            continue;
       }
       if (c == '\\')
       {
           if (*++str) {
            char buf[2]; 
            buf[0] = *str; 
            buf[1] = '\0';
            strcat ( wd, buf );
            ++str;
           }
            continue;
       }
       if (c == '$')
       {
            strcat ( wd , env_str = get_env(&str) ); 
            wdsize += strlen (env_str) - 1;
            k += strlen (env_str);
            continue;
       }
       wdsize++;
       res[i-1] = wd = (char*) realloc (wd,wdsize*sizeof(char));
       if (wd == NULL) {fprintf(stderr, "form_list: wd realloc err\n"); exit(1);}
       wd[k++] = *str++;
       wd[k]='\0'; //eol
    }
    if (quo) fprintf(stderr, "form_list: quotes were not closed\n");
    res[i] = NULL; // after last word pointer = NULL
    return res;
}

char* read_str (void) //read string from stdin
{
    fflush(stdin);
    char* buf = NULL; // buffer
    char* str = NULL; // string
    char* check = NULL; // check realloc on error
    char* ef = NULL; //check fgets on EOF
    char* el = NULL; //check fgets on EOL
    int count = 1;
    int i;
    buf = (char *) calloc (size,1);
    str = (char *) calloc (size,1);
    if (buf)
    do
    {
        el = ef = fgets(buf,size,stdin);
        if (ferror(stdin))
            { fprintf(stderr, "read_str: buf fgets err\n"); exit(1); }
        for (i = 0; i < size; ++i)
            if (buf[i] == '\n') //check on eol
                    el = NULL;
        check = (char *) realloc(str,count*size);
        if (check == NULL) 
            { fprintf(stderr, "read_str: str relloc err\n"); free(str); exit(1); }
        str = check; //str = check -> cur string
        if (ef) strcat(str,buf);
        count++;
    }while (ef && el);
    else 
        { fprintf(stderr, "read_str: buf or str calloc err\n"); exit(1); }
    free(buf);
    return str;
}

int print_list (char** s) //print list of strings
{
    if(s==NULL) return 0;
    int i;
    for(i = 0; s[i]; ++i){
        fputs( s[i], stdout );
        printf(" ");
    }
    printf("\n");
    return i; //words number
}

void sstr_freed(char **s) //clear list of strings
{
    if(s){
        int i;
        for (i = 0; s[i]; ++i)
             free(s[i]);
        free(s);
    }
}

void tree_freed(tree* t) //clear tree of cmd
{
   if(t){
    if( t->argv ) free ( t->argv );
    if( t->psub ) tree_freed( t->psub );
    if( t->pipe ) tree_freed( t->pipe );
    if( t->next ) tree_freed( t->next );
    free(t);
   }
   return;

}

void change_dir(char** s) //change directory
{
    if ( *(s+1) && **(s+1) != '~' )
    {
        int i;
        for (i = 1; *(s+i);++i)
            if ( chdir( *(s+i) ) < 0 ) perror("cd error");
    }
    else
    {
        char home_dir[1024] = "/home/";
        strcat(home_dir,getenv("USER") );
        if ( chdir( home_dir ) < 0 ) perror("cd error");
    }
}

void clear_zombie() //clear zombie
{
    int i,pid,st;
    int count = 0;
    for(i = bg_ps; i>0;--i)
    {
        pid = waitpid(-1,&st,WNOHANG);
        if ( WIFEXITED ( st ) || WIFSIGNALED( st ) ) //if zombie ps ended or killed - wait
        {                          
                waitpid(pid,&st,0);
                count++;
        }
    }
    bg_ps -=count;

}


int main(int argc, char** argv)
{
    char* str = NULL;  // string stdin
    char** sstr = NULL; //string of string
    tree* tr = NULL; //tree of cmd
    signal(SIGINT,SIG_IGN);
    int ef = 1; //check stdin on EOF
    int debug = 0; //debug mode
    char dir[1024];
    int c_arg = 0;
    if (argc > 1){
        c_arg = 1;
        char* check = NULL;
        int ssize = 0;
        int i;
        for (i = 1; i<argc; ++i)
        {   
            ssize = ssize + strlen(argv[i]);
            check = (char *) realloc(str,ssize+2);
            if (check == NULL) 
                { fprintf(stderr, "arg_str: str relloc err\n"); free(str); exit(1); }
            str = check; //str = check -> cur string
            strcat(str,argv[i]);
            if (i+1<argc) strcat(str," ");
        }
    }

    setjmp(begin);
    while (ef)
    {    
        if ( !c_arg ) {
            RST_GLBL; //reset globals
            free(str);
            sstr_freed(sstr); 
            tree_freed(tr); 
            clear_zombie();
            tr = NULL;
            str = NULL;
            sstr = NULL;
            printf ("%s$ ", getcwd(dir,1024) );  
            str = read_str();
        }else c_arg = 0; 
        
        if ( feof(stdin) || ( !strncmp(str,"exit",4) && ef-- ) )
        {
            if (ef)
            printf("\n");
            break;
        }
        
        if ( !strcmp(str,"debug\n") ){
            debug = debug == 1 ? 0 : 1;
            longjmp(begin,1);
        }

        sstr = form_list (str);
        
        if (debug){
            printf ("\nformed: ");
            print_list (sstr);
        }
        if ( check_list (sstr) )    //on syntax mistakes
            if ( !strcmp(*sstr,"cd") )
            {
                change_dir(sstr);
                longjmp(begin,1);
            }

        tr = build_tree (sstr);
        
        if (debug)
            print_tree(tr,NULL); //e,i,s,a for debug

        exec_tree(tr);
    
    }

    return 0;
}
