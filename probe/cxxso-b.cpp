/* cxxso-b.cpp -- libcxxsob.so.1, the second of tools/test-cxx-shared.sh's
 * C++ libraries.  It NEEDs libcxxsoa.so.1 and libstdc++.so.6.
 *
 * b_total lets libcxxsoa's exception pass through its own frames to the
 * caller; b_catch_from_a catches it here; b_call lets the program's own
 * exception pass through.  Each of those needs the one unwinder to know the
 * .eh_frame of every object on the stack.
 */
#include <vector>
#include "cxxso.h"

Circle::Circle(int radius) : r(radius) {}
Circle::~Circle() {}
int Circle::area() const { return 3 * r * r; }
const char *Circle::name() const { return "circle"; }

Shape *b_make_circle(int r)
{
	return new Circle(r);
}

int b_total(Shape *const *v, int n)
{
	int sum = 0;

	for (int i = 0; i < n; i++)
		sum += a_area_checked(v[i]);
	return sum;
}

int b_catch_from_a(int code)
{
	try {
		a_throw(code);
	} catch (const ShapeError &e) {
		return e.code;
	}
	return -1;
}

int b_call(int (*f)(int), int x)
{
	return f(x) + 1;
}

int b_vector_at(int i)
{
	std::vector<int> v(3, 7);

	return v.at(i);
}

/* The area of a square of side n, or minus it when libcxxsoa throws. */
extern "C" int b_c_api(int n)
{
	Shape *s = a_make_square(n);
	int a;

	try {
		a = a_area_checked(s);
	} catch (const std::exception &e) {
		const ShapeError *se = dynamic_cast<const ShapeError *>(&e);
		a = se ? -se->code : 0;
	}
	delete s;
	return a;
}
