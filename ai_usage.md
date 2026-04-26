# Documentație Utilizare AI — Faza 1

## Unealtă folosită
Claude (Anthropic) — accesat prin claude.ai

---

## Ce am cerut AI-ului să genereze

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

---

### 2. `match_condition()`

**Prompt-ul dat AI-ului:**
> Folosind același struct `Report` de mai sus, generează o funcție:
> `int match_condition(Report *r, const char *field, const char *op, const char *value);`
>
> Returnează 1 dacă raportul satisface condiția `field op value`, 0 altfel.
> Câmpuri suportate: `severity`, `category`, `inspector`, `timestamp`.
> Operatori suportați: `==`, `!=`, `<`, `<=`, `>`, `>=`.

**Ce a fost generat:**
AI-ul a generat o funcție care folosea `strcmp()` pentru toate comparațiile de câmpuri, inclusiv pentru `severity` și `timestamp`. Acest lucru este **incorect** pentru câmpurile numerice deoarece `strcmp("9", "10")` returnează o valoare pozitivă (deoarece `'9' > '1'` în ASCII), ceea ce înseamnă că 9 ar părea mai mare decât 10 — lucru greșit numeric.

**Ce am schimbat și de ce:**
- Pentru `severity` (int) și `timestamp` (time_t / long): parsez `value` cu `atoi()` / `atol()` și compar ca întregi.
- Pentru `category` și `inspector` (șiruri): am păstrat comparațiile cu `strcmp()` — corecte pentru câmpuri de tip string.
- Am adăugat un avertisment pentru nume de câmpuri necunoscute în loc să returnez silențios 0.

**Ce am învățat:**
Instrumentele AI confundă frecvent comparațiile numerice cu cele de șiruri atunci când valorile vin ca `char *` din linia de comandă. Trebuie întotdeauna verificat dacă semantica comparației corespunde tipului de date underlying. Aceasta este o greșeală clasică în C cu consecințe reale asupra corectitudinii filtrelor.

---

## Rezumat contribuție AI vs. student

| Parte | Generat de AI | Scris de student |
|-------|--------------|------------------|
| Scheletul `parse_condition` | Da | Nu |
| Corectarea bug-ului din `parse_condition` (strtok → strchr) | Nu | Da |
| Scheletul `match_condition` | Da | Nu |
| Corectarea bug-ului din `match_condition` (tipuri numerice) | Nu | Da |
| Tot restul codului (main, comenzi, permisiuni, logging, symlink-uri) | Nu | Da |