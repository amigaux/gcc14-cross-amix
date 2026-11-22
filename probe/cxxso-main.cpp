/* cxxso-main.cpp -- a C++ program using two C++ shared libraries that share
 * types, for tools/test-cxx-shared.sh.
 *
 * Linked with `g++ -shared-libgcc` (cxxso-main): it takes the C++ runtime
 * from libstdc++.so.6, as the libraries do, and every step prints ok.
 * Linked the default way (cxxso-main-static): it carries a static runtime
 * of its own, and libstdc++.so.6 stops it at start-up, before main, with a
 * message naming the fix.
 *
 * One line per step, flushed, so a crash names the step it died in.
 */
#include <stdio.h>
#include <stdexcept>
#include "cxxso.h"

static int failed;

static void step(int n, const char *what, bool ok)
{
	printf("%d %-52s %s\n", n, what, ok ? "ok" : "FAIL");
	fflush(stdout);
	if (!ok)
		failed = 1;
}

static int thrower(int x)
{
	if (x > 3)
		throw std::out_of_range("thrown in the program");
	return x;
}

int main()
{
	const char *seen = a_constructed();

	step(1, "libcxxsoa's constructor ran before main",
	     seen != 0 && seen[0] == 'l');

	Shape *sq = a_make_square(4), *ci = b_make_circle(2);
	step(2, "virtual calls into both libraries",
	     sq->area() == 16 && ci->area() == 12);
	step(3, "dynamic_cast to a type of libcxxsob",
	     dynamic_cast<Circle *>(ci) != 0 && dynamic_cast<Circle *>(sq) == 0);

	Shape *big = a_make_square(11);
	Shape *v[3] = { sq, ci, big };
	int code = 0;
	try {
		b_total(v, 3);
	} catch (const ShapeError &e) {
		code = e.code;
	}
	step(4, "thrown in libcxxsoa, through libcxxsob, caught here", code == 121);

	step(5, "thrown in libcxxsoa, caught in libcxxsob", b_catch_from_a(7) == 7);

	bool caught = false;
	try {
		b_call(thrower, 5);
	} catch (const std::logic_error &e) {
		caught = true;
	}
	step(6, "thrown here, through libcxxsob, caught by base class", caught);

	caught = false;
	try {
		b_vector_at(10);
	} catch (const std::out_of_range &e) {
		caught = true;
	}
	step(7, "thrown in libstdc++.so.6 (vector::at), caught here", caught);

	code = 0;
	try {
		try {
			a_throw(3);
		} catch (...) {
			throw;
		}
	} catch (const std::runtime_error &e) {
		const ShapeError *se = dynamic_cast<const ShapeError *>(&e);
		code = se ? se->code : -1;
	}
	step(8, "rethrown here, caught as std::runtime_error", code == 3);

	step(9, "what() built in libcxxsoa",
	     std::string(ShapeError("x", 0).what()) == "cxxso: x");

	delete sq;
	delete ci;
	delete big;
	printf("%s\n", failed ? "FAIL" : "PASS");
	fflush(stdout);
	return failed;
}
