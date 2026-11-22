/* cxxso.h -- the types two C++ shared libraries and their programs share,
 * for tools/test-cxx-shared.sh.
 *
 * libcxxsoa.so.1 (cxxso-a.cpp) defines Shape's and ShapeError's key
 * functions, so their vtables and type_info live there and nowhere else.
 * libcxxsob.so.1 (cxxso-b.cpp) NEEDs it and defines Circle.  The programs
 * catch ShapeError by its type and dynamic_cast to Circle, which only works
 * when every object agrees on one type_info and one C++ runtime.
 */
#ifndef CXXSO_H
#define CXXSO_H

#include <stdexcept>
#include <string>

struct Shape {
	virtual ~Shape();
	virtual int area() const = 0;
	virtual const char *name() const = 0;
};

struct Circle : Shape {
	int r;
	explicit Circle(int radius);
	~Circle();
	int area() const;
	const char *name() const;
};

struct ShapeError : std::runtime_error {
	int code;
	ShapeError(const std::string &what, int code);
	~ShapeError() noexcept;
};

/* libcxxsoa.so.1 */
Shape *a_make_square(int side);
int a_area_checked(const Shape *s);	/* throws ShapeError over 100 */
void a_throw(int code);			/* throws ShapeError(code) */
const char *a_constructed(void);	/* what its static object's constructor saw */

/* libcxxsob.so.1 */
Shape *b_make_circle(int r);
int b_total(Shape *const *v, int n);	/* a_area_checked of each: does not catch */
int b_catch_from_a(int code);		/* catches a_throw's ShapeError */
int b_call(int (*f)(int), int x);	/* calls back into the program */
int b_vector_at(int i);			/* std::vector::at, in libstdc++.so.6 */

/* libcxxsob.so.1, for a program in C. */
extern "C" int b_c_api(int n);

#endif
