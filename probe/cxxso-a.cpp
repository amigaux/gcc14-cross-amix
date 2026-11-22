/* cxxso-a.cpp -- libcxxsoa.so.1, the first of tools/test-cxx-shared.sh's two
 * C++ libraries.  Built with `g++ -shared -fPIC`, so it NEEDs libstdc++.so.6
 * and carries no C++ runtime of its own.
 *
 * Its static object prints through std::cout from its constructor: the
 * standard streams are libstdc++.so.6's, and they work there only if that
 * library's constructors ran first.  The runtime linker loads libstdc++.so.6
 * after this library, which NEEDs it, and the program's runner runs the
 * libraries' constructors last loaded first.
 */
#include <iostream>
#include <string>
#include "cxxso.h"

Shape::~Shape() {}

ShapeError::ShapeError(const std::string &what, int c)
	: std::runtime_error("cxxso: " + what), code(c) {}
ShapeError::~ShapeError() noexcept {}

namespace {
struct Square : Shape {
	int side;
	explicit Square(int s) : side(s) {}
	int area() const { return side * side; }
	const char *name() const { return "square"; }
};

struct Banner {
	std::string seen;
	Banner() : seen("libcxxsoa: static object constructed")
	{
		std::cout << seen << " (std::cout)" << std::endl;
	}
};
Banner banner;
}

Shape *a_make_square(int side)
{
	return new Square(side);
}

int a_area_checked(const Shape *s)
{
	int a = s->area();

	if (a > 100)
		throw ShapeError(std::string(s->name()) + " too big", a);
	return a;
}

void a_throw(int code)
{
	throw ShapeError("thrown in libcxxsoa", code);
}

const char *a_constructed(void)
{
	return banner.seen.c_str();
}
