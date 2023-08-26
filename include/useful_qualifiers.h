#ifndef USEFUL_QUALIFIERS_H_
#define USEFUL_QUALIFIERS_H_

#define TIME_SENSITIVE(x) __attribute__((section(".time_critical." #x))) x

#ifndef TIME_SENSITIVE
#define TIME_SENSITIVE(x) x
#endif

#endif /* USEFUL_QUALIFIERS_H_ */
