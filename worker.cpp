#include <iostream>
#include <stdlib.h>
#include <winsock2.h>
#include <process.h>
#include <string>
#include <fstream>
#include <map>
#include <cctype>
extern "C"
{
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
}
#pragma comment(lib, "lua51.lib")
#pragma comment(lib, "Ws2_32.lib")

using namespace std;

enum{STR_SIZE=256,PORT=7500,PORT_MONITORING=7501, ARBITERS_MAX=100};

struct actor
{
    string behavior;
    char parameters[5][50];
    lua_State *luaVM;
    int count;
};

struct dispatcher_answer
{
    int command;   //Команда, интерпретируется на стороне клиента.
    // 1 - create
    // 2 - send
    // 4 - print
    // 5 - finish print
    // 6 - Первоначальная рассылка скрипта(всем работникам)
    // 61- идет процесс пересылки скрипта
    // 62- процесс пересылки завершен
    // 7 - Запуск скрипта(одному работнику)
    int worker_id; //кому отправляем
    char arbiter_id[STR_SIZE]; //Арбитр, которому отправляем
    //actor actor_create_msg; //Параметры для передачи в create\become

    //Параметры actor
    char actor_behavior[STR_SIZE];
    char actor_parameters[5][50]; //Параметр для передачи в send
    /*char actor_behavior[STR_SIZE];
    char actor_addressat[STR_SIZE];
    int  actor_val;
    bool actor_isValSet;
    int  actor_parameter; //Параметр для передачи в send*/
    /*string actor_behavior;
    string actor_parameters[100];*/
    int actor_par_count;
    char script[1024];
};
///////////////////////
map <string,actor> actors;
char scriptFileName[STR_SIZE];
lua_State *g_LuaVM = NULL; //Глобальная, фактически это CreateAndInitActors, у всех остальных актеров свои переменные интерпритатора


int send_actor_obr(dispatcher_answer answer);
void sendAnswer(int command, char arbiter_id[STR_SIZE], actor act);
bool init_lua(char *scriptFileName, lua_State *&loc_luaVM);
void start_lua(char *func_name);
bool LoadScript(char *script);
void _lua_pushStringOrNumber(lua_State *g_LuaVM, const char *str);
void get_actor_func_name(char &func_name, bool &spec_func_name, lua_State *luaVM, const char *str, char send_parameters[5][50]);

///////////////////////
class _client
{
private:
    char p_adr[30];
    int domain;
    int type;
    int protocol;
public:
    char client_id[STR_SIZE];  // Получаем от сервера, для формирования id арбитров
    SOCKET my_sock,monitoring_sock;
    int arbiter_num; // Просто счетчик, увеличивается при создании арбитра. Имя арбитра: "client_id;arbiter_num"
    _client(char _p_adr[30]);
    bool connectToServer();
    bool connectToServerSecondSocket(string client_id);
    //bool mainCycle();
    bool readInput();
    char *getCin();
    char *generateArbiterId();
}*client;

_client::_client(char _p_adr[30])
{
    strcpy(p_adr,_p_adr);
    arbiter_num=0;
}
bool _client::connectToServer()
{
    int domain=AF_INET; //адресный домен Internet
    int type=SOCK_STREAM; //обсепечивает надёжный дуплексный протокол на основе установления логического соединения
    int protocol=0;//В контексте TCP/IP протокол явно определяется типом сокета, поэтому в кач-ве значения 0
    char buf[STR_SIZE]; //Строка получения данных
    char srv_resp[STR_SIZE];//Технические сообщения сервера
    //Структура для подключения
    struct sockaddr_in peer;
    peer.sin_family=domain;
    peer.sin_port=htons(PORT);    //Номер порта
    peer.sin_addr.s_addr=inet_addr(p_adr); //IP Адрес сервера

    //для select
    //+++++
    bool error_fl=false;
    WSADATA WsaData;
    int err = WSAStartup (0x0101, &WsaData);
    if (err == SOCKET_ERROR)
    {
        cout<<"#ERROR with WSAStrartup()";
        error_fl=true;
    }
    my_sock=socket(domain, type, protocol);

    if(my_sock<0)
    {
        cout<<"#ERROR with socket";
        error_fl=true;
    }

    if(connect(my_sock,(struct sockaddr *)&peer,sizeof(peer))<0) //Подключение не прошло
    {
        cout<<"#ERROR with connect";
        error_fl=true;
    }
    //Отправляем на сервер nick
    send(my_sock," ",1,0);
    //Получаем от сервера сообщение о подтверждении подключения
    recv(my_sock,srv_resp,STR_SIZE,0);
    bool quit=true;
    if(!error_fl && strcmp(srv_resp,"")!=0)
    {
        strcpy(client_id,srv_resp);
        // Подключение ко второму сокету
        if(connectToServerSecondSocket(client_id))
        {
            cout<<"Connection Established."<<endl;
            quit=false;
        }
    }
    return quit;
}

bool _client::connectToServerSecondSocket(string client_id)
{
    int domain=AF_INET; //адресный домен Internet
    int type=SOCK_STREAM; //обсепечивает надёжный дуплексный протокол на основе установления логического соединения
    int protocol=0;//В контексте TCP/IP протокол явно определяется типом сокета, поэтому в кач-ве значения 0
    char buf[STR_SIZE]; //Строка получения данных
    char srv_resp[STR_SIZE];//Технические сообщения сервера
    //Структура для подключения
    struct sockaddr_in peer;
    peer.sin_family=domain;
    peer.sin_port=htons(PORT_MONITORING);    //Номер порта
    peer.sin_addr.s_addr=inet_addr(p_adr); //IP Адрес сервера

    //для select
    //+++++
    bool error_fl=false;
    WSADATA WsaData;
    int err = WSAStartup (0x0101, &WsaData);
    if (err == SOCKET_ERROR)
    {
        cout<<"#ERROR with WSAStrartup()";
        error_fl=true;
    }
    monitoring_sock=socket(domain, type, protocol);

    if(monitoring_sock<0)
    {
        cout<<"#ERROR with socket MONITORING";
        error_fl=true;
    }

    if(connect(monitoring_sock,(struct sockaddr *)&peer,sizeof(peer))<0) //Подключение не прошло
    {
        cout<<"#ERROR with connect to MONITORING";
        error_fl=true;
    }
    //Отправляем на сервер nick
    send(monitoring_sock,client_id.c_str(),STR_SIZE,0);
    //Получаем от сервера сообщение о подтверждении подключения
    recv(monitoring_sock,srv_resp,STR_SIZE,0);
    bool good=false;
    if(!error_fl && strcmp(srv_resp,"done")==0)
    {
        //cout<<"Connection Established."<<endl;
        good=true;
    }
    return good;
}

bool _client::readInput()
{
   char *part_str;
   char buf[STR_SIZE]; //Строка получения данных
   struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
   fd_set readfds;
   bool quit=false;

   strcpy(buf,getCin());

   part_str=strtok(buf," ");//Выделяем первое слово до пробела. Это комманда
   //Вычисляем длинну комманды + символ пробела - для удаления
   if(part_str!=0)
   {
       int length_com=strlen(part_str);
       if(strcmp(part_str,"SEND")==0) //Команда на посыл сообщения
       {
            //Удаляем первые 5 символов "SEND " и записываем в buf сообщение
            part_str=strtok(NULL," ");
            char buf2[STR_SIZE]="";
            while(part_str!=NULL)
            {
                strcat(buf2,part_str);
                strcat(buf2," ");     //КРИВО, Т.К. ТОЛЬКО 1 ПРОБЕЛ БУДЕТ ВОЗМОЖЕН МЕЖДУ СЛОВАМИ
                part_str=strtok(NULL," ");
            }
            //Отправляем сообщение на сервер
            send(my_sock,buf2,STR_SIZE,0);
       }
       else if(strcmp(part_str,"QUIT")==0)
           quit=true;
   }
   return quit;
}

/*void sendSocket(SOCKET my_sock, dispatcher_answer *answer) //Просто отсылка сообщения через сокет
{
   char buf[STR_SIZE]; //Строка получения данных
   strcpy(buf,input);
   //Отправляем сообщение на сервер
   send(my_sock,buf,STR_SIZE,0);
}*/

void recv_file(SOCKET my_sock,int worker_id)
{
    bool finish=false;
    bool started=false;
    char buf[1024];
    dispatcher_answer answer;
    char *pBuff = new char[sizeof(dispatcher_answer)];
    char addr[STR_SIZE]="";
    char fname[STR_SIZE];
    _itoa(worker_id,fname,10);
    strcat(addr,fname);
    strcat(addr,".lua");
    strcpy(scriptFileName,addr);
    FILE *in = fopen(addr, "w");
    while (!finish)
    {
      int nbytes = recv( my_sock, pBuff, sizeof(answer), 0 );
      memcpy(&answer,pBuff,sizeof(dispatcher_answer));
      if ( nbytes == 0)
      {
        cout<<"Disconnected."<<endl;
        return;
      }
      if (nbytes < 0)
      {
       cout<<"Error: "<<WSAGetLastError()<<endl;
       return;
      }
      if(answer.command!=61)
          finish=true;
      else
          fwrite(answer.script,strlen(answer.script),1,in);
    }
    fclose(in);
    delete[] pBuff;
    return;
}


void readSocket(void *client)
{
    char *part_str;
    char buf[STR_SIZE]; //Строка получения данных
    struct timeval tv;
     tv.tv_sec = 0;
     tv.tv_usec = 0;
    fd_set readfds;
    bool quit=false;
    _client *my_client=(_client*) client;
    SOCKET my_sock=my_client->my_sock;
    SOCKET monitoringSock=my_client->monitoring_sock;
    dispatcher_answer answer;
    //Выполняем комманду select
    //Ф-я проверяет статус сокета my_sock.
    //Последний параметр 0 - ф-я select не замораживает
    //Работу программы, а просто один раз читает состояние сокета
    Sleep(1);
    bool fl=false;
    while(!fl)
    {
       //Очищаем readfds
       FD_ZERO(&readfds);
       //Заносим дескриптор сокета в readfds
       FD_SET(monitoringSock,&readfds);
       //Последний параметр - время ожидания. Выставляем нули чтобы
       //Select не блокировал выполнение программы до смены состояния сокета
       select(NULL,&readfds,NULL,NULL,&tv);
       //Если пришли данные на чтение то читаем
       int bytes_recv;
       char tmp[STR_SIZE];
       if(FD_ISSET(monitoringSock,&readfds))
       {
            //Проверяем не отключился ли сервер
            char *pBuff = new char[STR_SIZE];
            if((bytes_recv=recv(monitoringSock,pBuff,STR_SIZE,0)) &&(bytes_recv!=SOCKET_ERROR))
            {
                strcpy(tmp,pBuff);
                strcat(tmp,my_client->client_id);
                send(monitoringSock,tmp,STR_SIZE,0);
                fl=true;
            }
       }
    }

/*    while(1)
    {
        bool fl=false;
        Sleep(1);
        //Очищаем readfds
        FD_ZERO(&readfds);
        //Заносим дескриптор сокета в readfds
        FD_SET(my_sock,&readfds);
        while(!fl)
        {
           //Последний параметр - время ожидания. Выставляем нули чтобы
           //Select не блокировал выполнение программы до смены состояния сокета
           select(NULL,&readfds,NULL,NULL,&tv);
           //Если пришли данные на чтение то читаем
           int bytes_recv;
           char tmp[STR_SIZE];
           if(FD_ISSET(my_sock,&readfds))
           {
                //Проверяем не отключился ли сервер
                char *pBuff = new char[sizeof(dispatcher_answer)];
                if((bytes_recv=recv(my_sock,pBuff,sizeof(dispatcher_answer),0)) &&(bytes_recv!=SOCKET_ERROR))
                {
                    memcpy(&answer,pBuff,sizeof(dispatcher_answer));
                    //cout<<answer.actor_behavior<<endl;
                    switch(answer.command)
                    {
                        case 1: // Create
                            actors[answer.arbiter_id].behavior=string(answer.actor_behavior);
                            actors[answer.arbiter_id].count=answer.actor_par_count;
                            for(int i=0;i<answer.actor_par_count;i++)
                                strcpy(actors[answer.arbiter_id].parameters[i],answer.actor_parameters[i]);
                            init_lua(scriptFileName,actors[answer.arbiter_id].luaVM); // Инициализируем интерпритатор для конкретного актера
                            // Сохраняем index актера в глобальную переменную self
                            lua_pushstring(actors[answer.arbiter_id].luaVM, answer.arbiter_id);
                            lua_setglobal(actors[answer.arbiter_id].luaVM, "self");
                            break;
                        case 2: // Send
                            send_actor_obr(answer);
                            break;
                        case 3: // become
                            actors[answer.arbiter_id].behavior=answer.actor_behavior;
                            actors[answer.arbiter_id].count=answer.actor_par_count;
                            for(int i=0;i<answer.actor_par_count;i++)
                                strcpy(actors[answer.arbiter_id].parameters[i],answer.actor_parameters[i]);
                            break;
                        case 6: //Первоначальная рассылка скрипта
                            recv_file(my_sock,answer.worker_id); // Здесь имя файла заносится в глобальную scriptFileName
                            init_lua(scriptFileName,g_LuaVM);
                            break;
                        case 7: //Инициализация скрипта(старт вычислений)
                            start_lua("createAndInitActors"); //Всегда начинаем с функции creatAndInitActors
                            break;
                    }
                }
                else
                {
                     quit=true;
                     break;
                }
                delete[] pBuff;
           }
           else
               fl=true;
        }
    }*/
}

char *_client::getCin()
{
    _flushall();
    char buf[STR_SIZE],*p; //Строка получения данных
    cin.getline(buf,STR_SIZE);
    //++++Очистка буфера cin
    cin.clear();
    p=buf;
    return p;
}

char *_client::generateArbiterId()
{
    char p[STR_SIZE];
    char tmp[STR_SIZE];
    strcpy(p,client_id);
    strcat(p,";");
    sprintf(tmp,"%d",arbiter_num);
    strcat(p,tmp);
    arbiter_num++;
    return p;
}
//////////////////////////////

//++++Actor+++
//++++++++++++


bool LoadScript(char *script)
{

    int s = luaL_loadfile(g_LuaVM, scriptFileName);
  if(s != 0)
  {
    cout << "Error while loading script file!" << endl << "Error: " << lua_tostring(g_LuaVM, -1) << endl;

    return false;
  }

  // Выполняем крипт
  s = lua_pcall(g_LuaVM, 0, LUA_MULTRET, 0);

  return true;
}

int create_actor(lua_State *luaVM)
{
    // Получаем число переданных из Lua скрипта аргументов
    int argc = lua_gettop(luaVM);
    actor act;
    // Если аргументов меньше одного - возвращаем ошибку
    if(argc < 1)
    {
        cerr << "Create - wrong number of arguments!" << endl;
        // Вернем 0 в Lua скрипт
        lua_pushnumber(luaVM, 0);

        // Количество возвращаемых значений
        return 1;
    }

    act.behavior=lua_tostring(luaVM,1);
    int count=0;
    for(int i=2;i<=argc;i++)   //в lua параметры начинаются с 1, а первый мы уже приняли, поэтому i=2
    {
        if(!lua_isnil(luaVM,i))
            strcpy(act.parameters[i-2],lua_tostring(luaVM,i));
        else
            strcpy(act.parameters[i-2],"nil");
        count++;
    }
    act.count=count;

    char arbiterId[STR_SIZE];
    strcpy(arbiterId,client->generateArbiterId());
    sendAnswer(1,arbiterId,act);

    // Возвращаем в Lua скрипт индекс созданного актера
    lua_pushstring(luaVM, arbiterId);
    return 1;
}
int send_actor(lua_State *luaVM)
{
    // Получаем число переданных из Lua скрипта аргументов
    int argc = lua_gettop(luaVM);

    // Если аргументов меньше двух - возвращаем ошибку
    if(argc < 2)
    {
        cerr << "Send - wrong number of arguments!" << endl;
        // Вернем 0 в Lua скрипт
        lua_pushnumber(luaVM, 0);

        // Количество возвращаемых значений
        return 1;
    }
    char arbiterId[STR_SIZE];
    strcpy(arbiterId,lua_tostring(luaVM, 1)); //Получаем index актера которому передаем(1-й аргумент в send)
    int send_count=0;
    actor act;
    for(int i=2;i<=argc;i++)//
    {
        if(!lua_isnil(luaVM,i))
            strcpy(act.parameters[i-2],lua_tostring(luaVM,i));
        else
            strcpy(act.parameters[i-2],"nil");
        send_count++;
    }
    act.behavior="";
    act.count=send_count;
    sendAnswer(2,arbiterId,act);
    return 0;
}

bool is_number(const std::string& s)
{
    std::string::const_iterator it = s.begin();
    while (it != s.end() && isdigit(*it)) ++it;
    return !s.empty() && it == s.end();
}
int send_actor_obr(dispatcher_answer answer)
{
    char arbiter_id[STR_SIZE];
    strcpy(arbiter_id,answer.arbiter_id);
    string func=actors[arbiter_id].behavior;
    actor act;
    //act.behavior=answer.actor_behavior;
    act.count=answer.actor_par_count;
    for(int i=0;i<answer.actor_par_count;i++)
        strcpy(act.parameters[i],answer.actor_parameters[i]);
    if(func=="final_print")//Отправляем на сервер, что система отработала
    {
        sendAnswer(5,arbiter_id,act);
        actors.clear();
        client->arbiter_num=0;
    }
    else if(func=="print") //Просто вывод
    {
        sendAnswer(4,arbiter_id,act);
    }
    else
    {
        // Переместить на начало стека функцию поведения
        // Функций 2 типа: имяАктера или имяАктера_тип. Тип передается в send вторым параметром (первый - ссылка на актера).
        // в get_actor_func_name происходит выбор имени функции, если есть функция имяАктера_тип, то она возвращается в func_name, иначе имяАктера
        // флаг spec_func_name становится true если имя функции не соответствует имени актера, т.е. имяАктера_тип
        char func_name[100];
        bool spec_func_name=false;
        if(act.count!=0)
            get_actor_func_name(*func_name, spec_func_name, g_LuaVM,func.c_str(),act.parameters);
        else
            strcpy(func_name,func.c_str());

        lua_getglobal(actors[arbiter_id].luaVM, func_name); // Это перемещение на вершину стека функции поведения.
        //lua_isfunction(luaVM,lua_gettop(luaVM))
        //lua_pushstring(actors[arbiter_id].luaVM,arbiter_id); // self
        //Сначала передаем параметры которые были занесены в актера при создании
        for(int i=0;i<actors[arbiter_id].count;i++)
        {
            if(strcmp(actors[arbiter_id].parameters[i],"nil")==0)
                lua_pushnil(actors[arbiter_id].luaVM);
            else
            {
                    _lua_pushStringOrNumber(actors[arbiter_id].luaVM,actors[arbiter_id].parameters[i]);
            }
        }
        //Затем передаем параметры пришедшие с функцией send
        for(int i=0;i<act.count;i++)
        {
            if(spec_func_name==true && i==0) // Если имяАктера_тип, то первый параметр это тип, его не передаем.
                continue;
            if(strcmp(act.parameters[i],"nil")==0)
                lua_pushnil(actors[arbiter_id].luaVM);
            else
            {
                    _lua_pushStringOrNumber(actors[arbiter_id].luaVM,act.parameters[i]);
            }
        }

        if(spec_func_name==true)
            act.count--;
        if(lua_pcall(actors[arbiter_id].luaVM, actors[arbiter_id].count+(act.count), 0, 0) != 0)
        {
          // Проверить возвращенное значение,
          // если это не 0, сообщить об ошибке
          // lua_tostring(g_LuaVM, -1) содержит описание ошибки
          cerr << "Error calling function Send1: " << lua_tostring(g_LuaVM, -1) << endl;
        }
    }
    return 0;
}

void get_actor_func_name(char &func_name, bool &spec_func_name, lua_State *luaVM, const char *str, char send_parameters[5][50])
{   // У актеров 2 типа функций обработчиков: имяАктера или имяАктера_тип. Функция проверяет был ли передан тип в параметрах
    // И есть ли такая функция. Если есть возвращается имяАктера_тип, если нет возвращяется имяАктера.
    // За имя типа отвечает первый параметр в send_parameters

    // Проверяем является ли буквенно-цифровым первый параметр
    spec_func_name=false;
    bool alnum=true;
    for(int i=0;i<strlen(send_parameters[0]);i++)
    {
        if(!isalnum(send_parameters[0][i]))
            alnum=false;
    }
    if(!alnum)
        strcpy(&func_name,str);
    else
    {
        // Проверяем наличие функции с таким именем
        char catstr[100];
        strcpy(catstr,str);
        strcat(catstr,"_");
        strcat(catstr,send_parameters[0]);
        lua_getglobal(luaVM, catstr);
        if(lua_isfunction(luaVM,lua_gettop(luaVM)))
        {
            strcpy(&func_name,catstr);
            spec_func_name=true;
        }
        else
            strcpy(&func_name,str);
    }
}

void _lua_pushStringOrNumber(lua_State *g_LuaVM, const char *str)
{
    int len=strlen(str);
    bool digit=true;
    for(int i=0;i<len;i++)
        if(!isdigit(str[i]))
            digit=false;
    if(digit)
        lua_pushnumber(g_LuaVM,atoi(str));
    else
        lua_pushstring(g_LuaVM,str);
}

int become_actor(lua_State *luaVM)
{
    actor act;
    // Получаем число переданных из Lua скрипта аргументов
    int argc = lua_gettop(luaVM);
    // Если аргументов меньше двух - возвращаем ошибку
    if(argc < 1)
    {
        cerr << "Create - wrong number of arguments!" << endl;
        // Вернем 0 в Lua скрипт
        lua_pushnumber(luaVM, 0);

        // Количество возвращаемых значений
        return 1;
    }

    //Берем self из глобальных для этого актера
    string behavior=lua_tostring(luaVM,1);
    lua_getglobal(luaVM,"self");
    string index=lua_tostring( luaVM, lua_gettop( luaVM ));
    actors[index].behavior=behavior;
    int count=0;
    for(int i=2;i<=argc;i++)   //в lua параметры начинаются с 1, а первый мы уже приняли, поэтому i=2
    {
        if(!lua_isnil(luaVM,i))
            strcpy(actors[index].parameters[i-2],lua_tostring(luaVM,i));
        else
            strcpy(actors[index].parameters[i-2],"nil");
        count++;
    }
    actors[index].count=count;
    return 0;
}


void sendAnswer(int command, char arbiter_id[STR_SIZE], actor act)
{
    //Создаем структуру для отправки
    dispatcher_answer answer;
    answer.command=command;
    strcpy(answer.arbiter_id,arbiter_id);

    strcpy(answer.actor_behavior,act.behavior.c_str());

    answer.actor_par_count=act.count;
    for(int i=0;i<act.count;i++)
        strcpy(answer.actor_parameters[i],act.parameters[i]);

    char *pBuff = new char[sizeof(dispatcher_answer)];
    // заполняем sc
    memcpy( pBuff, &answer, sizeof(dispatcher_answer));
    send( client->my_sock, pBuff, sizeof(dispatcher_answer), 0 );
    delete[] pBuff;
}

bool init_lua(char *scriptFileName, lua_State *&loc_luaVM)
{
    // Инициализируем экземпляр
    loc_luaVM = lua_open();
    //
    lua_register(loc_luaVM, "create", create_actor);
    lua_register(loc_luaVM, "send", send_actor);
    lua_register(loc_luaVM, "become", become_actor);

    int s = luaL_loadfile(loc_luaVM, scriptFileName);

    if(s!=0)
    {
        cout << "Error while loading script file!" << endl << "Error: " << lua_tostring(g_LuaVM, -1) << endl;
        return false;
    }

    // Выполняем скрипт
    s = lua_pcall(loc_luaVM, 0, LUA_MULTRET, 0);

    return true;
}

void start_lua(char *func_name)
{
    //Вызываем main функцию lua скрипта
    // Переместить на начало стека функцию onFileFound
    lua_getglobal(g_LuaVM, func_name);
    // Поместить следующим элементом в стек путь к найденному файлу (fileName в скрипте)
    //lua_pushstring(g_LuaVM, strFilePath.c_str());

    // Вызвать функцию createAndInitActors
    lua_pcall(g_LuaVM, 0, 0, 0);
}

//++++++++++++

int main(int argc, char *argv[])
{
    // Считываем параметры при запуске программы
    char p_adr[30];
    if(argc==2)
       strcpy(p_adr,argv[1]);
    else
       strcpy(p_adr,"127.0.0.1");
    client = new _client(p_adr);

    bool quit=client->connectToServer();

    int ThreadID;
    _beginthread(readSocket,0,(void *)client); // Поток на чтение

    //init_lua();
    while(!quit)
    {
        quit=client->readInput(); //Чтение с клавиатуры
        Sleep(1);
    }
    lua_close(g_LuaVM);
    remove(scriptFileName);
    return 0;
}
//--------------------------------------------------------
