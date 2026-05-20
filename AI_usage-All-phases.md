# Documentație Utilizare AI — Toate Fazele

## Unealtă folosită

Claude (Anthropic) — accesat prin claude.ai

-----

## Faza 1 — Sisteme de Fișiere

### 1. `parse_condition()`

**Prompt-ul dat AI-ului:**

> Am un struct C numit `Report` cu câmpurile: `int id`, `char inspector[64]`, `double latitude`, `double longitude`, `char category[32]`, `int severity`, `time_t timestamp`, `char description[256]`.
> 
> Generează o funcție cu această semnătură:
> `int parse_condition(const char *input, char *field, char *op, char *value);`
> 
> Primește un șir de forma `field:op:value` (ex. `severity:>=:2`, `category:==:road`) și îl împarte în cele trei părți stocate în bufferele de ieșire. Returnează 1 la succes, 0 la eșec.

**Ce a fost generat:**
AI-ul a generat o versiune folosind `strtok()` pe `:` ca delimitator. A identificat corect cele trei părți, dar abordarea cu `strtok` avea un bug subtil: `strtok` modifică șirul original (inserează bytes `\0`), ceea ce ar corupe conținutul `argv[]` dacă condițiile erau parsate de mai multe ori.

**Ce am schimbat și de ce:**

- Am înlocuit `strtok` cu aritmetică manuală de pointeri folosind `strchr()` pentru a găsi cei doi separatori `:` fără a modifica șirul original.
- Am adăugat verificări de limite (lungimea câmpului, operatorului și valorii verificate față de dimensiunea bufferelor).
- Am adăugat o verificare pentru pointeri nuli la început.

**Ce am învățat:**
`strtok` este distructiv — nu trebuie folosit niciodată pe șiruri din `argv[]` sau buffere partajate. Folosirea `strchr` și `strncpy` cu calculul explicit al lungimii este mai sigură pentru parsarea formatelor cu delimitatori ficși.

-----

### 2. `match_condition()`

**Prompt-ul dat AI-ului:**

> Folosind același struct `Report` de mai sus, generează o funcție:
> `int match_condition(Report *r, const char *field, const char *op, const char *value);`
> 
> Returnează 1 dacă raportul satisface condiția `field op value`, 0 altfel.
> Câmpuri suportate: `severity`, `category`, `inspector`, `timestamp`.
> Operatori suportați: `==`, `!=`, `<`, `<=`, `>`, `>=`.

**Ce a fost generat:**
AI-ul a generat o funcție care folosea `strcmp()` pentru toate comparațiile de câmpuri, inclusiv pentru `severity` și `timestamp`. Acest lucru este incorect pentru câmpurile numerice deoarece `strcmp("9", "10")` returnează o valoare pozitivă, ceea ce înseamnă că 9 ar părea mai mare decât 10 — lucru greșit numeric.

**Ce am schimbat și de ce:**

- Pentru `severity` (int) și `timestamp` (time_t / long): parsez `value` cu `atoi()` / `atol()` și compar ca întregi.
- Pentru `category` și `inspector` (șiruri): am păstrat comparațiile cu `strcmp()`.
- Am adăugat un avertisment pentru nume de câmpuri necunoscute.

**Ce am învățat:**
Instrumentele AI confundă frecvent comparațiile numerice cu cele de șiruri când valorile vin ca `char *` din linia de comandă. Trebuie întotdeauna verificat dacă semantica comparației corespunde tipului de date underlying.

-----

### Rezumat Faza 1

|Parte                                                               |Generat de AI|Scris de student|
|--------------------------------------------------------------------|-------------|----------------|
|Scheletul `parse_condition`                                         |Da           |Nu              |
|Corectarea bug-ului din `parse_condition` (strtok → strchr)         |Nu           |Da              |
|Scheletul `match_condition`                                         |Da           |Nu              |
|Corectarea bug-ului din `match_condition` (tipuri numerice)         |Nu           |Da              |
|Tot restul codului (main, comenzi, permisiuni, logging, symlink-uri)|Nu           |Da              |

-----

## Faza 2 — Procese și Semnale

Nu am folosit AI pentru generarea codului din Faza 2.

Codul pentru `monitor_reports.c` și modificările din `city_manager.c` (`cmd_remove_district`, `notify_monitor`) au fost scrise de student cu ajutor AI pentru debugging și înțelegerea erorilor de compilare.

**Probleme întâlnite și rezolvate cu ajutor AI:**

- Eroare de compilare: `SIGUSR1 undeclared` — rezolvată adăugând `#include <signal.h>` și `#include <sys/wait.h>`
- Eroare: `notify_monitor` apelată înainte de declarare — rezolvată adăugând forward declaration
- Eroare: `static` declaration conflict — rezolvată eliminând `static` din definiția funcției
- Eroare de bash: `severity:>=:2` interpretat ca redirect de shell — rezolvată folosind ghilimele

**Ce am învățat:**

- `sigaction()` este mai sigur decât `signal()` pentru gestionarea semnalelor
- `write()` este signal-safe, `printf()` nu este — în handlere de semnale trebuie folosit `write()`
- `fork()` + `execlp()` + `waitpid()` pentru crearea de procese copil
- `kill(pid, SIGUSR1)` pentru trimiterea de semnale între procese

|Parte                                |Generat de AI|Scris de student|
|-------------------------------------|-------------|----------------|
|`monitor_reports.c` complet          |Nu           |Da              |
|`cmd_remove_district` în city_manager|Nu           |Da              |
|`notify_monitor` în city_manager     |Nu           |Da              |
|Debugging erori de compilare         |Ajutor AI    |Da              |

-----

## Faza 3 — Pipes și Redirectări

Nu am folosit AI pentru generarea logicii principale din Faza 3.

Codul pentru `city_hub.c` și `scorer.c` a fost scris cu ajutor AI pentru structura generală, iar studentul a revizuit și adaptat tot codul.

**Ce face fiecare program:**

- `scorer.c` — citește `reports.dat` dintr-un district și calculează workload score per inspector (suma severity-urilor). Scrie rezultatul pe stdout, care este redirectat prin pipe de către hub.
- `city_hub.c` — program interactiv cu comenzile `start_monitor` și `calculate_scores`. Folosește `fork()`, `pipe()`, `dup2()` și `exec*()` pentru a coordona procesele.

**Concepte folosite:**

- `pipe()` — crearea unui canal de comunicare între procese
- `dup2()` — redirectarea stdout-ului scorer-ului/monitorului către write-end-ul pipe-ului
- `fork()` + `execlp()` — crearea proceselor scorer și hub_mon
- `waitpid()` — așteptarea terminării proceselor copil
- Citirea din read-end-ul pipe-ului pentru a colecta output-ul scorer-ului

**Modificări aduse `monitor_reports.c` față de Faza 2:**

- Verifică la pornire dacă un monitor rulează deja (citind `.monitor_pid` și testând cu `kill(pid, 0)`)
- Dacă monitorul există deja, trimite un mesaj de eroare cu PID-ul existent și iese
- Acceptă opțional un file descriptor ca argument pentru a scrie pe pipe în loc de stdout

**Ce am învățat:**

- `dup2(pipefd[1], STDOUT_FILENO)` redirectează stdout-ul unui proces copil înainte de `exec`
- Trebuie închis write-end-ul pipe-ului în procesul parinte după fork, altfel `read()` nu returnează EOF
- `kill(pid, 0)` verifică dacă un proces există fără a-i trimite un semnal real

|Parte                          |Generat de AI|Scris de student         |
|-------------------------------|-------------|-------------------------|
|Structura generală `city_hub.c`|Ajutor AI    |Da (revizuit complet)    |
|`scorer.c`                     |Ajutor AI    |Da (revizuit complet)    |
|Modificări `monitor_reports.c` |Ajutor AI    |Da (revizuit complet)    |
|Logica pipe + dup2 + exec      |Ajutor AI    |Da (înțeles și verificat)|