```python
# look at tools/set_up_magics.ipynb
get_ipython().run_cell('# one_liner_str <too much code> \n')
None
```


    <IPython.core.display.Javascript object>


# Задачки на разную асинхронщину и не только. 

Про обработчики сигналов:
* <a href="#sig_pause" style="color:#856024"> `while(!stopped) pause();` </a>
* <a href="#sig_handler_fin" style="color:#856024"> Финализация в хендлере </a>
* <a href="#sig_while_true_1" style="color:#856024"> `while(!stopped); #1` </a>
* <a href="#sig_while_true_2" style="color:#856024"> `while(!stopped); #2` </a>
* <a href="#sig_check" style="color:#856024"> `sigtimedwait` </a>
* <a href="#sig_check_regular" style="color:#856024"> `sigtimedwait` 2 </a>

Разное:
* <a href="#read_piece" style="color:#856024"> Retryable read </a>


Про межпоточную синхронизацию.
* <a href="#sig_mutex" style="color:#856024"> Signal handler synchronization </a>
* <a href="#condvar_1" style="color:#856024"> Condvar 1 </a>
* <a href="#condvar_2" style="color:#856024"> Condvar 2 </a>
* <a href="#condvar_3" style="color:#856024"> Condvar 3 </a>






# <a name="sig_pause"></a> `while(!stopped) pause();`

Корректна ли программа с точки зрения асинхронной безопасности обработчиков сигналов?
То есть будет ли она завершаться по приходу сигнала?


```cpp
%%cpp sig_pause.c
%run gcc -g -O0 sig_pause.c -lpthread -o sig_pause.exe
%run # ./sig_pause.exe

#include <unistd.h>
#include <signal.h>

volatile sig_atomic_t stop = 0;

static void handler(int signum) {
    stop = 1;
}

int main(int argc, char* argv[]) {
    int signals[] = {SIGINT, SIGTERM, 0};
    for (int* signal = signals; *signal; ++signal) {
        sigaction(*signal, &(struct sigaction){.sa_handler=handler}, NULL);
    }
    while (!stop) {
        int a = 1;
        pause();
    }
    return 0;
}
```


Run: `gcc -g -O0 sig_pause.c -lpthread -o sig_pause.exe`



Run: `# ./sig_pause.exe`



Run: `#() gdb -ex="b 21" -batch --args ./sig_pause.exe`


<details>
<summary><b>Ответ</b></summary>
<p>
    
Как все из вас с недырявой памятью помнят, так делать нельзя, если сигнал придет между проверкой и `pause`, то мы на него не среагируем.
    
    
Воспроизвести ситуацию можно так:
    
* Запустим программу под gdb, чтобы заморозить ее в неудачный момент (на строке `int a = 1;`): `gdb -ex="b 21" -ex="handle SIGTERM nostop" -ex="handle SIGCHLD nostop" -ex=r --args ./sig_pause.exe`
* Пошлем сигнал, который должен приводить к немедленному завершению: `killall -SIGTERM sig_pause.exe`
* Жмем `c` в gdb. 
* Вот и все, мы не среагировали на сигнал.    
    
   
A еще, если тут не будет volatile, то while(!stop) может соптимизироваться до while(true)
    
</p>
</details>







```python

```

# <a name="sig_handler_fin"></a> Финализация в хендлере

Корректна ли программа с точки зрения асинхронной безопасности обработчиков сигналов? То есть будет ли она завершаться по приходу сигнала? Будут ли освобождены ресурсы?


```cpp
%%cpp sig_handler_fin.c
%run gcc -g -O0 sig_handler_fin.c -lpthread -o sig_handler_fin.exe
%run ./sig_handler_fin.exe

#include <unistd.h>
#include <stdio.h>
#include <signal.h>

volatile sig_atomic_t resource = -1;

int resource_acquire() {
    static int res = 100; ++res;
    dprintf(2, "Resource %d acquired\n", res);
    return res;
}
void resource_release(int res) {
    dprintf(2, "Resource %d released\n", res);
}

static void handler(int signum) {
    if (signum == SIGUSR1) return;
    if (resource != -1) resource_release(resource);
    _exit(0);
}

int main(int argc, char* argv[]) {
    int signals[] = {SIGINT, SIGTERM, SIGUSR1, 0};
    for (int* signal = signals; *signal; ++signal) {
        sigaction(*signal, &(struct sigaction){.sa_handler=handler}, NULL);
    }
    while (1) {
        resource = resource_acquire(); // ~ accept
        sleep(1);
        resource_release(resource); // ~ shutdown & close
    }
    return 0;
}
```


Run: `gcc -g -O0 sig_handler_fin.c -lpthread -o sig_handler_fin.exe`



Run: `./sig_handler_fin.exe`


    Resource 101 acquired
    Resource 101 released
    Resource 102 acquired
    Resource 102 released
    Resource 103 acquired
    Resource 102 released


<details>
<summary><b>Ответ</b></summary>
<p>
    
Сигнал может прийти и обработаться после acquire и до сохранения в атомик. То есть завершаться будет, но ресурсы могут не быть освобождены.

Воспроизвести можно так: 
* вставляем `pause();` перед `return res;`. Таким образом, можно руками воспроизвести ситуацию, когда обработчик выполнится во время исполнения определенной строчки основного потока.
* `killall -SIGUSR1 sig_handler_fin.exe` - так можно пропустить столько первых вхождений в pause, сколько мы хотим.
* `killall -SIGTERM sig_handler_fin.exe` - завершаем программу. При этом обработчик вызовется в "неудачный" момент. И тогда последний выделенный ресурс не освободится. А освободится второй раз ресурс выделенный в прошлый раз, что тоже может как-то выстрелить.
</p>
</details>






# <a name="sig_while_true_1"></a> `while(!stopped); #1`

Корректна ли программа с точки зрения асинхронной безопасности обработчиков сигналов? 


```cpp
%%cpp sig_while_true.c
%run gcc -g -O3 sig_while_true.c -lpthread -o sig_while_true.exe
%run timeout -s SIGTERM 1 ./sig_while_true.exe

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

sig_atomic_t stop = 0;

static void handler(int signum) {
    stop = 1;
}

int main(int argc, char* argv[]) {
    int signals[] = {SIGINT, SIGTERM, 0};
    for (int* signal = signals; *signal; ++signal) {
        sigaction(*signal, &(struct sigaction){.sa_handler=handler}, NULL);
    }
    while (!stop);
    printf("Stopped\n");
    return 0;
}
```


Run: `gcc -g -O3 sig_while_true.c -lpthread -o sig_while_true.exe`



Run: `timeout -s SIGTERM 1 ./sig_while_true.exe`


    ^C


<details>
<summary><b>Ответ</b></summary>
<p>

Полный треш. Без volatile цикл оптимизируется и программа зависнет навсегда.

</p>
</details>






# <a name="sig_while_true_2"></a> `while(!stopped); #2`

Корректна ли программа с точки зрения асинхронной безопасности обработчиков сигналов? 


```cpp
%%cpp sig_while_true.c
%run gcc -g -O0 sig_while_true.c -lpthread -o sig_while_true.exe
%run timeout -s SIGTERM 1 ./sig_while_true.exe

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

volatile sig_atomic_t stop = 0;

static void handler(int signum) {
    stop = 1;
}

int main(int argc, char* argv[]) {
    int signals[] = {SIGINT, SIGTERM, 0};
    for (int* signal = signals; *signal; ++signal) {
        sigaction(*signal, &(struct sigaction){.sa_handler=handler}, NULL);
    }
    while (!stop);
    printf("Stopped\n");
    return 0;
}
```


Run: `gcc -g -O0 sig_while_true.c -lpthread -o sig_while_true.exe`



Run: `timeout -s SIGTERM 1 ./sig_while_true.exe`


    Stopped


<details>
<summary><b>Ответ</b></summary>
<p>

Программа верна с точки зрения безопасности.

Но она отвратительна с точки зрения производительности, такой код выдет выедать одно ядро процессора, что в значительной степени просадит всю производительность машинки.

За такой код в продакшне можно отрывать руки.

</p>
</details>






# <a name="sig_check"></a> `sigtimedwait`

Корректна ли программа с точки зрения асинхронной безопасности обработчиков сигналов?

То есть будет ли она завершаться по приходу сигнала? Будут ли освобождены ресурсы?


```cpp
%%cpp sig_check.c
%run gcc -g sig_check.c -o sig_check.exe
%run ./sig_check.exe 

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

int resource_acquire() {
    char c;
    read(0, &c, 1); // think, that here is accept-like logic (waiting external clients)
    dprintf(2, "Resource %d acquired\n", (int)c);
    return (int)c;
}
void resource_release(int res) {
    dprintf(2, "Resource %d released\n", res);
}

int main() {
    sigset_t full_mask;
    sigfillset(&full_mask);
    sigprocmask(SIG_BLOCK, &full_mask, NULL); 
    siginfo_t info;
    struct timespec timeout = {0};
    // В данном случае sigtimedwait проверяет, а не пришел ли сигнал. Работает без ожиданий
    while (sigtimedwait(&full_mask, &info, &timeout) < 0) {
        int resource = resource_acquire(); // ~ accept
        sleep(1);
        resource_release(resource); // ~ shutdown & close
    }   
    return 0;
}
```


Run: `gcc -g sig_check.c -o sig_check.exe`



Run: `./sig_check.exe`


    ^C
    Resource 10 acquired


<details>
<summary><b>Ответ</b></summary>
<p>

С точки зрения асинхронной безопасности может и отчасти корректна, но в целом некорректна, так как не решает задачу.
Так как если не будет клиентов, мы навсегда зависнем в acquire и не будем реагировать на сигналы.

То есть завершаться не будет. Но ресурсы будет освобождать хорошо.

</p>
</details>






# <a name="sig_check_regular"></a> `sigtimedwait 2`

Корректна ли программа с точки зрения асинхронной безопасности обработчиков сигналов?

То есть будет ли она завершаться по приходу сигнала? Будут ли освобождены ресурсы?


```cpp
%%cpp sig_check_2.c
%run gcc -g sig_check_2.c -o sig_check_2.exe
%run echo "0123" | ./sig_check_2.exe 

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

sigset_t full_mask;
const struct timespec timeout = {.tv_sec = 0, .tv_nsec = 10000000};
siginfo_t info;

int resource_acquire() {
    char c; int read_res;
    while ((read_res = read(0, &c, 1)) < 0) {
        if (sigtimedwait(&full_mask, &info, &timeout) >= 0) {
            return -1;
        }
    }
    if (read_res == 0) {
        return -1;
    }
    dprintf(2, "Resource %d acquired\n", (int)c);
    return (int)c;
}
void resource_release(int res) {
    dprintf(2, "Resource %d released\n", res);
}

int main() {
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    
    sigfillset(&full_mask);
    sigprocmask(SIG_BLOCK, &full_mask, NULL); 

    // В данном случае sigtimedwait проверяет, а не пришел ли сигнал. Работает без ожиданий
    while (sigtimedwait(&full_mask, &info, &timeout) < 0) {
        int resource = resource_acquire(); // ~ accept
        if (resource < 0) { break; }
        sleep(1);
        resource_release(resource); // ~ shutdown & close
    }   
    return 0;
}
```


Run: `gcc -g sig_check_2.c -o sig_check_2.exe`



Run: `echo "0123" | ./sig_check_2.exe`


    Resource 48 acquired
    ^C
    Resource 48 released


<details>
<summary><b>Ответ</b></summary>
<p>

С точки зрения асинхронной безопасности корректна. Будет завершаться и освобождать ресурсы.

Но! Это скользкая дорожка. Поскольку здесь мы проверяем ситуацию раз в некоторое время. Из-за этого реагируем на события с опозданием. Если будем уменьшать таймаут, то начнем зря тратить все большее количество CPU. Получается порочный tradeoff.

Не надо так делать, если есть возможность этого избежать.

</p>
</details>


# <a name="read_piece"></a> Retryable read

Будет ли корректно читать все, что доступно для чтения в socket_fd?


```cpp
%%cpp read_piece.c

union message {
    int i;
    char arr[4];
};
union message value;
  
//...

int readed_count = 0;
while(readed_count < 4) {
    r = read(socket_fd, &value + readed_count, sizeof(value) - readed_count);
    if (r > 0) {
        readed_count += r;
    } else if (r < 0) {
        assert(errno == EAGAIN);
    } else if (readed_count == 0) {
        return 0;
    }
}
```

<details>
<summary><b>Ответ</b></summary>
<p>

Нет, здесь баг в арифметике указателей.

`&value + readed_count` при `readed_count == 1` это указатель находящийся уже за пределами value. Исправляется так: `(char*)&value + readed_count` 

</p>
</details>



```python

```

# <a name="sig_mutex"></a> Signal handler synchronization

Есть ли тут асинхронная безопасность?


```cpp
%%cpp sig_mutex.c
%run gcc -g -O0 sig_mutex.c -lpthread -o sig_mutex.exe
%run # через 1 секунду пошлется SIGINT, через 2 SIGKILL
%run timeout -s SIGKILL 2 timeout -s SIGINT 1 ./sig_mutex.exe

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
long long int value = 0;

static void handler(int signum) {
    pthread_mutex_lock(&mutex);
    printf("Value = %lld\n", value);
    pthread_mutex_unlock(&mutex);
    exit(0);
}

int main(int argc, char* argv[]) {
    sigaction(SIGINT, &(struct sigaction){.sa_handler=handler}, NULL);
    while (1) {
        pthread_mutex_lock(&mutex);
        ++value;
        pthread_mutex_unlock(&mutex);
    }
    printf("Stopped\n");
    return 0;
}
```


Run: `gcc -g -O0 sig_mutex.c -lpthread -o sig_mutex.exe`



Run: `# через 1 секунду пошлется SIGINT, через 2 SIGKILL`



Run: `timeout -s SIGKILL 2 timeout -s SIGINT 1 ./sig_mutex.exe`


    Value = 4587181


<details>
<summary><b>Ответ</b></summary>
<p>

Нет, тут скрестили ужа с ежом. 
Обработчики сигнала выполняются в том потоке, куда пришел сигнал. (То есть выполнение в этом потоке прерывается, работает обработчик, потом выполнение возобновляется.)
Если в то время когда пришел сигнал, мьютекс захвачен, то хендлер навсегда зависнет на попытке его захватить.

Как воспроизвести некорректность? Вставьте `sched_yield();` после `++value;` и запустите.

</p>
</details>



```python

```

# <a name="condvar_1"></a> Condvar 1 

Есть ли тут асинхронная безопасность?


```cpp
%%cpp condvar.c
%run gcc -fsanitize=thread condvar.c -lpthread -o condvar.exe
%run ./condvar.exe

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t condvar = PTHREAD_COND_INITIALIZER;
_Atomic int value = -1;

static void* producer_func(void* arg) 
{
    atomic_store(&value, 42);
    pthread_cond_signal(&condvar);
}

static void* consumer_func(void* arg) 
{
    pthread_mutex_lock(&mutex);
    while (atomic_load(&value) == -1) {
        pthread_cond_wait(&condvar, &mutex); 
    }
    printf("Value = %d\n", atomic_load(&value));
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main()
{
    pthread_t producer_thread;
    pthread_create(&producer_thread, NULL, producer_func, NULL);
    
    pthread_t consumer_thread;
    pthread_create(&consumer_thread, NULL, consumer_func, NULL);

    pthread_join(producer_thread, NULL); 
    pthread_join(consumer_thread, NULL); 
    return 0;
}
```


Run: `gcc -fsanitize=thread condvar.c -lpthread -o condvar.exe`



Run: `./condvar.exe`


    Value = 42


<details>
<summary><b>Ответ</b></summary>
<p>

Нет, так как присвоение в атомик и сигнализация может произойти в одном потоке между `while (atomic_load(&value) == -1)` и `pthread_cond_wait(&condvar, &mutex);` в другом.

Воспроизведение: вставьте `sleep(2);` сразу после `while (value == -1) {`. И `sleep(1);` до `atomic_store(&value, 42);`.

</p>
</details>


# <a name="condvar_2"></a> Condvar 2

Есть ли тут асинхронная безопасность?


```cpp
%%cpp condvar_2.c
%run gcc -fsanitize=thread condvar_2.c -lpthread -o condvar_2.exe
%run ./condvar_2.exe

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t condvar = PTHREAD_COND_INITIALIZER;
_Atomic int value = -1;

static void* producer_func(void* arg) 
{
    pthread_mutex_lock(&mutex);
    atomic_store(&value, 42);
    pthread_cond_signal(&condvar);
    pthread_mutex_unlock(&mutex);
}

static void* consumer_func(void* arg) 
{
    pthread_mutex_lock(&mutex);
    while (atomic_load(&value) == -1) {
        pthread_cond_wait(&condvar, &mutex); 
    }
    printf("Value = %d\n", atomic_load(&value));
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main()
{
    pthread_t producer_thread;
    pthread_create(&producer_thread, NULL, producer_func, NULL);
    
    pthread_t consumer_thread;
    pthread_create(&consumer_thread, NULL, consumer_func, NULL);

    pthread_join(producer_thread, NULL); 
    pthread_join(consumer_thread, NULL); 
    return 0;
}
```


Run: `gcc -fsanitize=thread condvar_2.c -lpthread -o condvar_2.exe`



Run: `./condvar_2.exe`


    Value = 42


<details>
<summary><b>Ответ</b></summary>
<p>

Тут есть.

</p>
</details>


# <a name="condvar_3"></a> Condvar 3

Есть ли тут асинхронная безопасность?


```cpp
%%cpp condvar_3.c
%run gcc -fsanitize=thread condvar_3.c -lpthread -o condvar_3.exe
%run ./condvar_3.exe

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t condvar = PTHREAD_COND_INITIALIZER;
_Atomic int value = -1;

static void* producer_func(void* arg) 
{
    atomic_store(&value, 42);
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    pthread_cond_signal(&condvar);
}

static void* consumer_func(void* arg) 
{
    pthread_mutex_lock(&mutex);
    while (atomic_load(&value) == -1) {
        pthread_cond_wait(&condvar, &mutex); 
    }
    printf("Value = %d\n", atomic_load(&value));
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main()
{
    pthread_t producer_thread;
    pthread_create(&producer_thread, NULL, producer_func, NULL);
    
    pthread_t consumer_thread;
    pthread_create(&consumer_thread, NULL, consumer_func, NULL);

    pthread_join(producer_thread, NULL); 
    pthread_join(consumer_thread, NULL); 
    return 0;
}
```


Run: `gcc -fsanitize=thread condvar_3.c -lpthread -o condvar_3.exe`



Run: `./condvar_3.exe`


    Value = 42


<details>
<summary><b>Ответ</b></summary>
<p>

Как-ни странно, но добавление пустой критической секции тоже спасает ситуацию. Пустая критическая секция позволяет подождать, пока другой поток войдет в wait.

</p>
</details>



```python

```


```python

```


```python

```