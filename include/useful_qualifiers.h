#ifndef USEFUL_QUALIFIERS_H_
#define USEFUL_QUALIFIERS_H_

#define TIME_SENSITIVE(x) __not_in_flash_func(x)

#ifndef TIME_SENSITIVE
#define TIME_SENSITIVE(x) x
#endif

#endif /* USEFUL_QUALIFIERS_H_ */
