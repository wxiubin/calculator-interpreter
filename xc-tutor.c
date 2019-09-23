#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int token;				// current token
char *src, *old_src;	// pointer to source code string
int poolsize;			// defaults size of text/data/stack
int line;				// line number

int *text,				// text segment
	*old_text,			// for dump text segment
	*stack;				// stack
char *data;				// data segment

int *pc, *bp, *sp, ax, cycle;	// virtual machine registers

// instuctions
enum { LEA, IMM, JMP, CALL, JZ, JNZ, ENT, ADJ, LEV, LI, LC, SI, SC, PUSH, 
		OR, XOR, AND, EQ, NE, LT, GT, LE, GE, SHL, SHR, ADD, SUB, MUL, DIV, MOD, 
		OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT};

// tokens and classes (operators last and in precedence order)
enum {
	Num = 128, Fun, Sys, Glo, Loc, Id,
	Char, Else, Enum, If, Int, Return, Sizeof, While,
	Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

int token_val;
int *current_id, *symbols;
enum { Token, Hash, Name, Type, Class, Value, BType, BClass, BValue, IdSize };
/* 因为自举不打算支持 struct，所以 用数组 symbols 存放 struct identifier 信息 */
//struct identifier {
//	int token;	// 标识符返回的标记
//	int hash;	// 标识符哈希值，用于标识符的快速比较
//	char *name;	// 标识符本身的字符串
//	int class;	// 类别；比如 数字，全局变量或局部变量
//	int type;	// 类型；int or char
//	int value;	// 标识符的值；如果标识符是函数，则存放地址
//	int Bclass;	// 全局标识符信息
//	int Btype;
//	int Bvalue;
//}


/// 用于词法分析，获取下一个标记，它将自动忽略空白字符
void next() {
	char *last_pos;
	int hash;
	
	while (token = *src) {
		src++;
		if (token == '\n') {
			++line;
		}
		else if (token == '#') {
			// skip macro, because we will not support it
			while (*src != 0 && *src != '\n') {
				src++;
			}
		}
		else if ((token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') || (token ==  '_')) {
			// parse identifier
			last_pos = src - 1;
			hash = token;
			
			while ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9') || (*src ==  '_')) {
				hash = hash * 147 + *src;
				src++;
			}
			
			// look for existing identifier, linear search
			current_id = symbols;
			while (current_id[Token]) {
				if (current_id[Hash] == hash && !memcmp((char *)current_id[Name], last_pos, src - last_pos)) {
					// found one, return
					token = current_id[Token];
					return;
				}
				current_id = current_id + IdSize;
			}
			
			// store new ID
			current_id[Name] = (int)last_pos;
			current_id[Hash] = hash;
			token = current_id[Token] = Id;
			return;
		}
		else if (token >= '0' && token <= '9') {
			// parse number, three kinds: dec(123) hex(0x123) oct(017)
			token_val = token - '0';
			if (token_val > 0) {
				// dec, starts with [1-9]
				while (*src >= '0' && *src <= '9') {
					token_val = token_val * 10 + *src++ - '0';
				}
			} else {
				// starts with number 0
				if (*src == 'x' || *src == 'X') {
					// hex
					token = *++src;
					while ((token >= '0' && token <= '9') || (token >= 'a' && token <= 'f') || (token >= 'A' && token <= 'F')) {
						token_val = token_val * 16 + (token & 15) + (token >= 'A' ? 9 : 0);
						token = *++src;
					}
				} else {
					// oct
					while (*src >= '0' && *src <= '7') {
						token_val = token_val * 8 + *src - '0';
					}
				}
			}
			token = Num;
		}
		else if (token == '"' || token == '\'') {
			// parse string literal, currently, the only supported escape
			// character is '\n', store the string literal into data.
			last_pos = data;
			while (*src != 0 && *src != token) {
				token_val = *src++;
				if (token_val == '\\') {
					// escape character
					token_val = *src++;
					if (token_val == 'n') {
						token_val = '\n';
					}
				}

				if (token == '"') {
					*data++ = token_val;
				}
			}

			src++;
			// if it is a single character, return Num token
			if (token == '"') {
				token_val = (int)last_pos;
			} else {
				token = Num;
			}

			return;
		}
		else if (token == '/') {
			if (*src == '/') {
				// skip comments
				while (*src != 0 && *src != '\n') {
					++src;
				}
			} else {
				// divide operator
				token = Div;
				return;
			}
		}
		else if (token == '=') {
			// parse '==' and '='
			if (*src == '=') {
				src ++;
				token = Eq;
			} else {
				token = Assign;
			}
			return;
		}
		else if (token == '+') {
			// parse '+' and '++'
			if (*src == '+') {
				src ++;
				token = Inc;
			} else {
				token = Add;
			}
			return;
		}
		else if (token == '-') {
			// parse '-' and '--'
			if (*src == '-') {
				src ++;
				token = Dec;
			} else {
				token = Sub;
			}
			return;
		}
		else if (token == '!') {
			// parse '!='
			if (*src == '=') {
				src++;
				token = Ne;
			}
			return;
		}
		else if (token == '<') {
			// parse '<=', '<<' or '<'
			if (*src == '=') {
				src ++;
				token = Le;
			} else if (*src == '<') {
				src ++;
				token = Shl;
			} else {
				token = Lt;
			}
			return;
		}
		else if (token == '>') {
			// parse '>=', '>>' or '>'
			if (*src == '=') {
				src ++;
				token = Ge;
			} else if (*src == '>') {
				src ++;
				token = Shr;
			} else {
				token = Gt;
			}
			return;
		}
		else if (token == '|') {
			// parse '|' or '||'
			if (*src == '|') {
				src ++;
				token = Lor;
			} else {
				token = Or;
			}
			return;
		}
		else if (token == '&') {
			// parse '&' and '&&'
			if (*src == '&') {
				src ++;
				token = Lan;
			} else {
				token = And;
			}
			return;
		}
		else if (token == '^') {
			token = Xor;
			return;
		}
		else if (token == '%') {
			token = Mod;
			return;
		}
		else if (token == '*') {
			token = Mul;
			return;
		}
		else if (token == '[') {
			token = Brak;
			return;
		}
		else if (token == '?') {
			token = Cond;
			return;
		}
		else if (token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token == ':') {
			// directly return the character as token;
			return;
		}
	}
	token = *src++;
	
	return;
}

void match(int tk) {
	if (token == tk) {
		next();
	} else {
		printf("%d: expected token: %d\n", line, tk);
		exit(-1);
	}
}

int expr();

int factor() {
	int value = 0;
	if (token == '(') {
		match('(');
		value = expr();
		match(')');
	} else {
		value = token_val;
		match(Num);
	}
	return value;
}

int term_tail(int lvalue) {
	if (token == '*') {
		match('*');
		int value = lvalue * factor();
		return term_tail(value);
	} else if (token == '/') {
		match('/');
		int value = lvalue / factor();
		return term_tail(value);
	} else {
		return lvalue;
	}
}

int term() {
	int lvalue = factor();
	return term_tail(lvalue);
}

int expr_tail(int lvalue) {
	if (token == '+') {
		match('+');
		int value = lvalue + term();
		return expr_tail(value);
	} else if (token == '-') {
		match('-');
		int value = lvalue - term();
		return expr_tail(value);
	} else {
		return lvalue;
	}
}

int expr() {
	int lvalue = term();
	return expr_tail(lvalue);
}

/// 用于解析一个表达式
void expression(int level) {
	// do nothing
}

// types of variable/function
enum { CHAR, INT, PTR };

int basetype;    // the type of a declaration, make it global for convenience
int expr_type;   // the type of an expression

void enum_declaration() {
	// parse enum [id] { a = 1, b = 3, ...}
	int i;
	i = 0;
	while (token != '}') {
		if (token != Id) {
			printf("%d: bad enum identifier %d\n", line, token);
			exit(-1);
		}
		next();
		if (token == Assign) {
			// like {a=10}
			next();
			if (token != Num) {
				printf("%d: bad enum initializer\n", line);
				exit(-1);
			}
			i = token_val;
			next();
		}

		current_id[Class] = Num;
		current_id[Type] = INT;
		current_id[Value] = i++;

		if (token == ',') {
			next();
		}
	}
}

int index_of_bp; // index of bp pointer on stack

void statement() {
	// there are 6 kinds of statements here:
	// 1. if (...) <statement> [else <statement>]
	// 2. while (...) <statement>
	// 3. { <statement> }
	// 4. return xxx;
	// 5. <empty statement>;
	// 6. expression; (expression end with semicolon)

	int *a, *b; // bess for branch control

	if (token == If) {
		// if (...) <statement> [else <statement>]
		//
		//   if (...)           <cond>
		//                      JZ a
		//     <statement>      <statement>
		//   else:              JMP b
		// a:                 a:
		//     <statement>      <statement>
		// b:                 b:
		//
		//
		match(If);
		match('(');
		expression(Assign);  // parse condition
		match(')');

		// emit code for if
		*++text = JZ;
		b = ++text;

		statement();         // parse statement
		if (token == Else) { // parse else
			match(Else);

			// emit code for JMP B
			*b = (int)(text + 3);
			*++text = JMP;
			b = ++text;

			statement();
		}

		*b = (int)(text + 1);
	}
	else if (token == While) {
		//
		// a:                     a:
		//    while (<cond>)        <cond>
		//                          JZ b
		//     <statement>          <statement>
		//                          JMP a
		// b:                     b:
		match(While);

		a = text + 1;

		match('(');
		expression(Assign);
		match(')');

		*++text = JZ;
		b = ++text;

		statement();

		*++text = JMP;
		*++text = (int)a;
		*b = (int)(text + 1);
	}
	else if (token == '{') {
		// { <statement> ... }
		match('{');

		while (token != '}') {
			statement();
		}

		match('}');
	}
	else if (token == Return) {
		// return [expression];
		match(Return);

		if (token != ';') {
			expression(Assign);
		}

		match(';');

		// emit code for return
		*++text = LEV;
	}
	else if (token == ';') {
		// empty statement
		match(';');
	}
	else {
		// a = b; or function_call();
		expression(Assign);
		match(';');
	}
}

void function_body() {
	// type func_name (...) {...}
	//                   -->|   |<--

	// ... {
	// 1. local declarations
	// 2. statements
	// }

	int pos_local; // position of local variables on the stack.
	int type;
	pos_local = index_of_bp;

	// ①
	while (token == Int || token == Char) {
		// local variable declaration, just like global ones.
		basetype = (token == Int) ? INT : CHAR;
		match(token);

		while (token != ';') {
			type = basetype;
			while (token == Mul) {
				match(Mul);
				type = type + PTR;
			}

			if (token != Id) {
				// invalid declaration
				printf("%d: bad local declaration\n", line);
				exit(-1);
			}
			if (current_id[Class] == Loc) {
				// identifier exists
				printf("%d: duplicate local declaration\n", line);
				exit(-1);
			}
			match(Id);

			// store the local variable
			current_id[BClass] = current_id[Class]; current_id[Class]  = Loc;
			current_id[BType]  = current_id[Type];  current_id[Type]   = type;
			current_id[BValue] = current_id[Value]; current_id[Value]  = ++pos_local;   // index of current parameter

			if (token == ',') {
				match(',');
			}
		}
		match(';');
	}

	// ②
	// save the stack size for local variables
	*++text = ENT;
	*++text = pos_local - index_of_bp;

	// statements
	while (token != '}') {
		statement();
	}

	// emit code for leaving the sub function
	*++text = LEV;
}

void function_parameter() {
	int type;
	int params;
	params = 0;
	while (token != ')') {
		// ①

		// int name, ...
		type = INT;
		if (token == Int) {
			match(Int);
		} else if (token == Char) {
			type = CHAR;
			match(Char);
		}

		// pointer type
		while (token == Mul) {
			match(Mul);
			type = type + PTR;
		}

		// parameter name
		if (token != Id) {
			printf("%d: bad parameter declaration\n", line);
			exit(-1);
		}
		if (current_id[Class] == Loc) {
			printf("%d: duplicate parameter declaration\n", line);
			exit(-1);
		}

		match(Id);

		//②
		// store the local variable
		current_id[BClass] = current_id[Class]; current_id[Class]  = Loc;
		current_id[BType]  = current_id[Type];  current_id[Type]   = type;
		current_id[BValue] = current_id[Value]; current_id[Value]  = params++;   // index of current parameter

		if (token == ',') {
			match(',');
		}
	}

	// ③
	index_of_bp = params+1;
}

void function_declaration() {
	// type func_name (...) {...}
	//               | this part

	match('(');
	function_parameter();
	match(')');
	match('{');
	function_body();
	//match('}');                 //  ①

	// ②
	// unwind local variable declarations for all local variables.
	current_id = symbols;
	while (current_id[Token]) {
		if (current_id[Class] == Loc) {
			current_id[Class] = current_id[BClass];
			current_id[Type]  = current_id[BType];
			current_id[Value] = current_id[BValue];
		}
		current_id = current_id + IdSize;
	}
}

void global_declaration() {
	// global_declaration ::= enum_decl | variable_decl | function_decl
	//
	// enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'} '}'
	//
	// variable_decl ::= type {'*'} id { ',' {'*'} id } ';'
	//
	// function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'


	int type; // tmp, actual type for variable
	int i; // tmp

	basetype = INT;

	// parse enum, this should be treated alone.
	if (token == Enum) {
		// enum [id] { a = 10, b = 20, ... }
		match(Enum);
		if (token != '{') {
			match(Id); // skip the [id] part
		}
		if (token == '{') {
			// parse the assign part
			match('{');
			enum_declaration();
			match('}');
		}

		match(';');
		return;
	}

	// parse type information
	if (token == Int) {
		match(Int);
	}
	else if (token == Char) {
		match(Char);
		basetype = CHAR;
	}

	// parse the comma seperated variable declaration.
	while (token != ';' && token != '}') {
		type = basetype;
		// parse pointer type, note that there may exist `int ****x;`
		while (token == Mul) {
			match(Mul);
			type = type + PTR;
		}

		if (token != Id) {
			// invalid declaration
			printf("%d: bad global declaration\n", line);
			exit(-1);
		}
		if (current_id[Class]) {
			// identifier exists
			printf("%d: duplicate global declaration\n", line);
			exit(-1);
		}
		match(Id);
		current_id[Type] = type;

		if (token == '(') {
			current_id[Class] = Fun;
			current_id[Value] = (int)(text + 1); // the memory address of function
			function_declaration();
		} else {
			// variable declaration
			current_id[Class] = Glo; // global variable
			current_id[Value] = (int)data; // assign memory address
			data = data + sizeof(int);
		}

		if (token == ',') {
			match(',');
		}
	}
	next();
}

/// 语法分析的入口，分析整个 C 语言程序
void program() {
	// get next token
	next();
	while (token > 0) {
		global_declaration();
	}
}

/// 虚拟机的入口，用于解释目标代码
int eval() {
	int op, *tmp;
	while (1) {
		if (op == IMM) 		{ax = *pc++;}									// load immediate value to ax
		else if (op == LC) 	{ax = *(char *)ax;}								// load character to ax, address in ax
		else if (op == LI) 	{ax = *(int *)ax;}								// load integer to ax, address in ax
		else if (op == SC) 	{ax = *(char *)*sp++ = ax;}						// save character to address, value in ax, address on stack
		else if (op == SI) 	{*(int *)*sp++ = ax;}							// save integer to address, value in ax, address on stack
		else if (op == PUSH){*--sp = ax;}									// push the value of ax onto the stack
		else if (op == JMP) {pc = (int *)*pc;}								// jump to the address
		else if (op == JZ)	{pc = ax ? pc + 1 : (int *)*pc;}				// jump if ax is zero
		else if (op == JNZ) {pc = ax ? (int *)*pc : pc + 1;}				// jump if ax is zero
		else if (op == CALL){*--sp = (int)(pc + 1); pc = (int *)*pc;}		// call subroutine
//		else if (op == RET) {pc = (int *)*sp++;}							// return from subroutine
		else if (op == ENT) {*--sp = (int)bp; bp = sp; sp = sp - *pc++;}	// make new stack frame
		else if (op == ADJ) {sp = sp + *pc++;}								// add esp <size>
		else if (op == LEV) {sp = bp; bp = (int *)*sp++; pc = (int *)*sp++;}// restore call frame and PC
		else if (op == LEA) {ax = (int)(bp + *pc++);}						// load address for arguments
		
		else if (op == OR)  ax = *sp++ | ax;
		else if (op == XOR) ax = *sp++ ^ ax;
		else if (op == AND) ax = *sp++ & ax;
		else if (op == EQ)  ax = *sp++ == ax;
		else if (op == NE)  ax = *sp++ != ax;
		else if (op == LT)  ax = *sp++ < ax;
		else if (op == LE)  ax = *sp++ <= ax;
		else if (op == GT)  ax = *sp++ >  ax;
		else if (op == GE)  ax = *sp++ >= ax;
		else if (op == SHL) ax = *sp++ << ax;
		else if (op == SHR) ax = *sp++ >> ax;
		else if (op == ADD) ax = *sp++ + ax;
		else if (op == SUB) ax = *sp++ - ax;
		else if (op == MUL) ax = *sp++ * ax;
		else if (op == DIV) ax = *sp++ / ax;
		else if (op == MOD) ax = *sp++ % ax;
		
		else if (op == EXIT) { printf("exit(%d)", *sp); return *sp;}
		else if (op == OPEN) { ax = open((char *)sp[1], sp[0]); }
		else if (op == CLOS) { ax = close(*sp);}
		else if (op == READ) { ax = read(sp[2], (char *)sp[1], *sp); }
		else if (op == PRTF) { tmp = sp + pc[1]; ax = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); }
		else if (op == MALC) { ax = (int)malloc(*sp);}
		else if (op == MSET) { ax = (int)memset((char *)sp[2], sp[1], *sp);}
		else if (op == MCMP) { ax = memcmp((char *)sp[2], (char *)sp[1], *sp);}
		
		else {
			printf("unknown instruction:%d\n", op);
			return -1;
		}
	}
	return 0;
}


int *idmain;				// the 'main' function

int main(int argc, char *argv[]) {
	int i, fd;
	argc--;
	argv++;
	
	poolsize = 256 * 1024; // arbitrary size;
	line = 1;
	
	if ((fd = open(*argv, 0)) < 0) {
		printf("could not open(%s)\n", *argv);
		return -1;
	}
	
	if (!(src = old_src = malloc(poolsize))) {
		printf("could not malloc(%d) for source area\n", poolsize);
		return -1;
	}
	
	if ((i = read(fd, src, poolsize-1)) <= 0) {
		printf("read() return %d\n", i);
		return -1;
	}
	
	src[i] = 0; // add EOF character
	close(fd);
	
	// allocate memory for virtual machine
	if (!(text = old_text = malloc(poolsize))) {
		printf("could not malloc(%d) for text area\n", poolsize);
		return -1;
	}
	
	if (!(data = malloc(poolsize))) {
		printf("could not malloc(%d) for data area\n", poolsize);
		return -1;
	}
	
	if (!(stack = malloc(poolsize))) {
		printf("could not malloc(%d) for stack area\n", poolsize);
		return -1;
	}
	
	memset(text, 0, poolsize);
	memset(data, 0, poolsize);
	memset(stack, 0, poolsize);
	
	bp = sp = (int *)((int)stack + poolsize);
	ax = 0;
	
	
	src = "char else enum if int return sizeof while"
		  "open read close printf malloc memset memcmp exit void main";
		
	// add keywords to symbol table
	i = Char;
	while (i <= While) {
		next();
		current_id[Token] = i++;
	}
	
	// add library to symbol table
	i = OPEN;
	while (i <= EXIT) {
		next();
		current_id[Class] = Sys;
		current_id[Type] = INT;
		current_id[Value] = i++;
	}
	
	next(); current_id[Token] = Char; 	// handle void type
	next(); idmain = current_id; 		// keep track of main
	
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, stdin)) > 0) {
		src = line;
		next();
		printf("%d\n", expr());
	}
	
	program();
	return eval();
}