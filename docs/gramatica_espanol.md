# Formas de premisas en español

El analizador traduce las siguientes expresiones a lógica proposicional:

| Conector | Formas reconocidas | Fórmula |
| --- | --- | --- |
| Negación | `no`, `nunca`, `jamás`, `es falso que`, `es mentira que`, `no ocurre que` | `¬p` |
| Conjunción | `y`, `además`, `también`, `aunque`, `sin embargo`, `así como`, `junto con`, `tanto ... como ...` | `p ∧ q` |
| Disyunción | `o`, `o bien`, `ya sea ... o ...` | `p ∨ q` |
| Disyunción exclusiva | `o ... o ... pero no ambos`, `o bien ... o bien ... pero no ambos` | `p ⊻ q` |
| NOR | `ni ... ni ...` | `¬(p ∨ q)` |
| Condicional | `si p entonces q`, `si p, q`, `en caso de que p, q`, `siempre que p, q`, `con tal de que p, q`, `cuando p, q` | `p → q` |
| Condicional por suficiencia | `p es condición suficiente para q`, `p basta para q`, `p implica q` | `p → q` |
| Condicional por necesidad | `q es condición necesaria para p`, `p solo si q`, `p únicamente si q` | `p → q` |
| Bicondicional | `si y solo si`, `si y solamente si`, `si y únicamente si`, `equivale a`, `es equivalente a`, `condición necesaria y suficiente` | `p ↔ q` |

Las tildes se normalizan antes del análisis. Una oración que no coincide con
un patrón de conector se conserva como un átomo completo; por ejemplo,
`la puerta está abierta` se convierte en una sola proposición.

## Normalización de conjugaciones

Antes de crear un átomo, el motor normaliza formas verbales conocidas a un
lema común. Por ejemplo, `llueve`, `está lloviendo`, `llovió` y `lloverá`
representan `llover`, por lo que pueden compararse en reglas diferentes.
También se incluyen formas frecuentes de los verbos del dominio, como
`arranca`, `se mueve`, `detecta`, `enciende`, `suena`, `abre`, `hace`, `puede`,
`tiene`, `viene` y sus formas progresivas o pasadas.

Los reemplazos se hacen sobre palabras completas para no modificar palabras
que solo contienen una secuencia similar. Las formas irregulares se agregan
al léxico explícitamente. Esto es lematización orientada a proposiciones, no
un diccionario completo ni un análisis sintáctico del español; una nueva forma
verbal debe incorporarse al léxico antes de que pueda compartir átomo.

La interpretación de `o` es inclusiva salvo que aparezca explícitamente
`pero no ambos`. Por tanto, `p o q` permite que ambas proposiciones sean
verdaderas.

## Memorias guardadas (UI)

La interfaz ahora permite:

- Guardar múltiples entradas con **[ GUARDAR MEMORIA ]**
- Seleccionar una memoria del panel **06 MEMORIAS GUARDADAS**
- Cargarla con **[ CARGAR MEMORIA ]** para reanalizarla
- Borrarla con **[ BORRAR MEMORIA ]**
- Mantener exportación manual con **[ EXPORTAR JSON ]**

Las memorias persisten en `memorias_guardadas.db`.

## Corpus de referencia (+200 frases)

Se incluye un corpus de más de 200 frases con su regla lógica en
[corpus_frases_220.tsv](./corpus_frases_220.tsv).
Este archivo sirve como banco de entrenamiento y validación manual de
formas lingüísticas.
