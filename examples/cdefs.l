WHITESPACE	\s+
ILCOMMENT	"//"(.|\\\r?\n)*
MLCOMMENT	"/*"([^*]|(\*+[^*/]))*\*+\/
DIRECTIVE	^[ \t]*#(.|\\\r?\n)+
NAME		[\u\l_]\w*
UDEC		0|[1-9]\d*
UHEX		0[Xx][[:xdigit:]]+
UOCT		0[0-7]+
INT		[-+]?({UDEC}|{UHEX})([Ll]{0,2}[Uu]?|[Uu][Ll]{0,2})
FLOAT		[-+]\d*(\d|\.\d|\d\.)\d*([Ee][-+]?\d+)?[FfLl]?
CHAR		L?'(\\.|[^\\\n'])*'
STRING		L?\"(\\.|\\\r?\n|[^\\\n"])*\"