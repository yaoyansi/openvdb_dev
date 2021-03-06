///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////

#include <cppunit/extensions/HelperMacros.h>
#include <openvdb/Types.h>
#include <openvdb/openvdb.h>
#include <openvdb/tools/Composite.h>
#include "util.h" // for unittest_util::makeSphere()

#define TEST_CSG_VERBOSE 0

#if TEST_CSG_VERBOSE
#include <tbb/tick_count.h> // Timer
#endif

namespace {
typedef openvdb::tree::Tree4<float, 4, 3, 3>::Type Float433Tree;
typedef openvdb::Grid<Float433Tree> Float433Grid;
}


class TestTreeCombine: public CppUnit::TestFixture
{
public:
    virtual void setUp() { openvdb::initialize(); Float433Grid::registerGrid(); }
    virtual void tearDown() { openvdb::uninitialize(); }

    CPPUNIT_TEST_SUITE(TestTreeCombine);
    CPPUNIT_TEST(testCombine);
    CPPUNIT_TEST(testCombine2);
    CPPUNIT_TEST(testCompMax);
    CPPUNIT_TEST(testCompMin);
    CPPUNIT_TEST(testCompSum);
    CPPUNIT_TEST(testCompProd);
    CPPUNIT_TEST(testCompReplace);
    CPPUNIT_TEST(testBoolTree);
#ifdef DWA_OPENVDB
    CPPUNIT_TEST(testCsg);
#endif
    CPPUNIT_TEST_SUITE_END();

    void testCombine();
    void testCombine2();
    void testCompMax();
    void testCompMin();
    void testCompSum();
    void testCompProd();
    void testCompReplace();
    void testBoolTree();
    void testCsg();

private:
    template<class TreeT, typename TreeComp, typename ValueComp>
    void testComp(const TreeComp&, const ValueComp&);

    template<class TreeT>
    void testCompRepl();

    template<typename TreeT, typename VisitorT>
    typename TreeT::Ptr
    visitCsg(const TreeT& a, const TreeT& b, const TreeT& ref, const VisitorT&);

#if TEST_CSG_VERBOSE
    struct Timer {
        tbb::tick_count t;
        Timer(): t(tbb::tick_count::now()) {}
        void start() { t = tbb::tick_count::now(); }
        double stop() { return (tbb::tick_count::now() - t).seconds(); };
    };
#endif
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestTreeCombine);


////////////////////////////////////////


namespace {
namespace Local {

template<typename ValueT>
struct OrderDependentCombineOp {
    OrderDependentCombineOp() {}
    void operator()(const ValueT& a, const ValueT& b, ValueT& result) const {
        result = a + 100 * b; // result is order-dependent on A and B
    }
};

/// Test Tree::combine(), which takes a functor that accepts three arguments
/// (the a, b and result values).
template<typename TreeT>
void combine(TreeT& a, TreeT& b)
{
    a.combine(b, OrderDependentCombineOp<typename TreeT::ValueType>());
}

/// Test Tree::combineExtended(), which takes a functor that accepts a single
/// CombineArgs argument, in which the functor can return a computed active state
/// for the output value.
template<typename TreeT>
void extendedCombine(TreeT& a, TreeT& b)
{
    typedef typename TreeT::ValueType ValueT;
    struct ArgsOp {
        static void order(openvdb::CombineArgs<ValueT>& args) {
            // The result is order-dependent on A and B.
            args.setResult(args.a() + 100 * args.b());
            args.setResultIsActive(args.aIsActive() || args.bIsActive());
        }
    };
    a.combineExtended(b, ArgsOp::order);
}

template<typename TreeT> void compMax(TreeT& a, TreeT& b) { openvdb::tools::compMax(a, b); }
template<typename TreeT> void compMin(TreeT& a, TreeT& b) { openvdb::tools::compMin(a, b); }
template<typename TreeT> void compSum(TreeT& a, TreeT& b) { openvdb::tools::compSum(a, b); }
template<typename TreeT> void compMul(TreeT& a, TreeT& b) { openvdb::tools::compMul(a, b); }\

float orderf(float a, float b) { return a + 100 * b; }
float maxf(float a, float b) { return std::max(a, b); }
float minf(float a, float b) { return std::min(a, b); }
float sumf(float a, float b) { return a + b; }
float mulf(float a, float b) { return a * b; }

openvdb::Vec3f orderv(const openvdb::Vec3f& a, const openvdb::Vec3f& b) { return a + 100 * b; }
openvdb::Vec3f maxv(const openvdb::Vec3f& a, const openvdb::Vec3f& b) {
    const float aMag = a.lengthSqr(), bMag = b.lengthSqr();
    return (aMag > bMag ? a : (bMag > aMag ? b : std::max(a, b)));
}
openvdb::Vec3f minv(const openvdb::Vec3f& a, const openvdb::Vec3f& b) {
    const float aMag = a.lengthSqr(), bMag = b.lengthSqr();
    return (aMag < bMag ? a : (bMag < aMag ? b : std::min(a, b)));
}
openvdb::Vec3f sumv(const openvdb::Vec3f& a, const openvdb::Vec3f& b) { return a + b; }
openvdb::Vec3f mulv(const openvdb::Vec3f& a, const openvdb::Vec3f& b) { return a * b; }

} // namespace Local
} // unnamed namespace


void
TestTreeCombine::testCombine()
{
    testComp<openvdb::FloatTree>(Local::combine<openvdb::FloatTree>, Local::orderf);
    testComp<openvdb::VectorTree>(Local::combine<openvdb::VectorTree>, Local::orderv);

    testComp<openvdb::FloatTree>(Local::extendedCombine<openvdb::FloatTree>, Local::orderf);
    testComp<openvdb::VectorTree>(Local::extendedCombine<openvdb::VectorTree>, Local::orderv);
}


void
TestTreeCombine::testCompMax()
{
    testComp<openvdb::FloatTree>(Local::compMax<openvdb::FloatTree>, Local::maxf);
    testComp<openvdb::VectorTree>(Local::compMax<openvdb::VectorTree>, Local::maxv);
}


void
TestTreeCombine::testCompMin()
{
    testComp<openvdb::FloatTree>(Local::compMin<openvdb::FloatTree>, Local::minf);
    testComp<openvdb::VectorTree>(Local::compMin<openvdb::VectorTree>, Local::minv);
}


void
TestTreeCombine::testCompSum()
{
    testComp<openvdb::FloatTree>(Local::compSum<openvdb::FloatTree>, Local::sumf);
    testComp<openvdb::VectorTree>(Local::compSum<openvdb::VectorTree>, Local::sumv);
}


void
TestTreeCombine::testCompProd()
{
    testComp<openvdb::FloatTree>(Local::compMul<openvdb::FloatTree>, Local::mulf);
    testComp<openvdb::VectorTree>(Local::compMul<openvdb::VectorTree>, Local::mulv);
}


void
TestTreeCombine::testCompReplace()
{
    testCompRepl<openvdb::FloatTree>();
    testCompRepl<openvdb::VectorTree>();
}


template<typename TreeT, typename TreeComp, typename ValueComp>
void
TestTreeCombine::testComp(const TreeComp& comp, const ValueComp& op)
{
    typedef typename TreeT::ValueType ValueT;

    const ValueT
        zero = openvdb::zeroVal<ValueT>(),
        minusOne = zero + (-1),
        minusTwo = zero + (-2),
        one = zero + 1,
        three = zero + 3,
        four = zero + 4,
        five = zero + 5;

    {
        TreeT aTree(/*background=*/one);
        aTree.setValueOn(openvdb::Coord(0, 0, 0), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 1), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 2), aTree.background());
        aTree.setValueOn(openvdb::Coord(0, 1, 2), aTree.background());
        aTree.setValueOff(openvdb::Coord(1, 0, 0), three);
        aTree.setValueOff(openvdb::Coord(1, 0, 1), three);

        TreeT bTree(five);
        bTree.setValueOn(openvdb::Coord(0, 0, 0), minusOne);
        bTree.setValueOn(openvdb::Coord(0, 1, 0), four);
        bTree.setValueOn(openvdb::Coord(0, 1, 2), minusTwo);
        bTree.setValueOff(openvdb::Coord(1, 0, 0), minusOne);
        bTree.setValueOff(openvdb::Coord(1, 1, 0), four);

        // Call aTree.compMax(bTree), aTree.compSum(bTree), etc.
        comp(aTree, bTree);

        // a = 3 (On), b = -1 (On)
        CPPUNIT_ASSERT_EQUAL(op(three, minusOne), aTree.getValue(openvdb::Coord(0, 0, 0)));

        // a = 3 (On), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(three, five), aTree.getValue(openvdb::Coord(0, 0, 1)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 0, 1)));

        // a = 1 (On, = bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(one, five), aTree.getValue(openvdb::Coord(0, 0, 2)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 0, 2)));

        // a = 1 (On, = bg), b = -2 (On)
        CPPUNIT_ASSERT_EQUAL(op(one, minusTwo), aTree.getValue(openvdb::Coord(0, 1, 2)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 1, 2)));

        // a = 1 (bg), b = 4 (On)
        CPPUNIT_ASSERT_EQUAL(op(one, four), aTree.getValue(openvdb::Coord(0, 1, 0)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 1, 0)));

        // a = 3 (Off), b = -1 (Off)
        CPPUNIT_ASSERT_EQUAL(op(three, minusOne), aTree.getValue(openvdb::Coord(1, 0, 0)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1, 0, 0)));

        // a = 3 (Off), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(three, five), aTree.getValue(openvdb::Coord(1, 0, 1)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1, 0, 1)));

        // a = 1 (bg), b = 4 (Off)
        CPPUNIT_ASSERT_EQUAL(op(one, four), aTree.getValue(openvdb::Coord(1, 1, 0)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1, 1, 0)));

        // a = 1 (bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(one, five), aTree.getValue(openvdb::Coord(1000, 1, 2)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1000, 1, 2)));
    }

    // As above, but combining the A grid into the B grid
    {
        TreeT aTree(/*bg=*/one);
        aTree.setValueOn(openvdb::Coord(0, 0, 0), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 1), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 2), aTree.background());
        aTree.setValueOn(openvdb::Coord(0, 1, 2), aTree.background());
        aTree.setValueOff(openvdb::Coord(1, 0, 0), three);
        aTree.setValueOff(openvdb::Coord(1, 0, 1), three);

        TreeT bTree(five);
        bTree.setValueOn(openvdb::Coord(0, 0, 0), minusOne);
        bTree.setValueOn(openvdb::Coord(0, 1, 0), four);
        bTree.setValueOn(openvdb::Coord(0, 1, 2), minusTwo);
        bTree.setValueOff(openvdb::Coord(1, 0, 0), minusOne);
        bTree.setValueOff(openvdb::Coord(1, 1, 0), four);

        // Call bTree.compMax(aTree), bTree.compSum(aTree), etc.
        comp(bTree, aTree);

        // a = 3 (On), b = -1 (On)
        CPPUNIT_ASSERT_EQUAL(op(minusOne, three), bTree.getValue(openvdb::Coord(0, 0, 0)));

        // a = 3 (On), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(five, three), bTree.getValue(openvdb::Coord(0, 0, 1)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 0, 1)));

        // a = 1 (On, = bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(five, one), bTree.getValue(openvdb::Coord(0, 0, 2)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 0, 2)));

        // a = 1 (On, = bg), b = -2 (On)
        CPPUNIT_ASSERT_EQUAL(op(minusTwo, one), bTree.getValue(openvdb::Coord(0, 1, 2)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 1, 2)));

        // a = 1 (bg), b = 4 (On)
        CPPUNIT_ASSERT_EQUAL(op(four, one), bTree.getValue(openvdb::Coord(0, 1, 0)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 1, 0)));

        // a = 3 (Off), b = -1 (Off)
        CPPUNIT_ASSERT_EQUAL(op(minusOne, three), bTree.getValue(openvdb::Coord(1, 0, 0)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1, 0, 0)));

        // a = 3 (Off), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(five, three), bTree.getValue(openvdb::Coord(1, 0, 1)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1, 0, 1)));

        // a = 1 (bg), b = 4 (Off)
        CPPUNIT_ASSERT_EQUAL(op(four, one), bTree.getValue(openvdb::Coord(1, 1, 0)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1, 1, 0)));

        // a = 1 (bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(op(five, one), bTree.getValue(openvdb::Coord(1000, 1, 2)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1000, 1, 2)));
    }
}


////////////////////////////////////////


void
TestTreeCombine::testCombine2()
{
    using openvdb::Vec3d;

    struct Local {
        static void floatAverage(const float& a, const float& b, float& result)
            { result = 0.5 * (a + b); }
        static void vec3dAverage(const Vec3d& a, const Vec3d& b, Vec3d& result)
            { result = 0.5 * (a + b); }
    };

    openvdb::FloatTree aFloatTree(/*bg=*/1.0), bFloatTree(5.0), outFloatTree(1.0);
    aFloatTree.setValue(openvdb::Coord(0, 0, 0), 3.0);
    aFloatTree.setValue(openvdb::Coord(0, 0, 1), 3.0);
    bFloatTree.setValue(openvdb::Coord(0, 0, 0), -1.0);
    bFloatTree.setValue(openvdb::Coord(0, 1, 0), 4.0);
    outFloatTree.combine2(aFloatTree, bFloatTree, Local::floatAverage);

    const float tolerance = 0.0;
    // Average of set value 3 and set value -1
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, outFloatTree.getValue(openvdb::Coord(0, 0, 0)), tolerance);
    // Average of set value 3 and bg value 5
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, outFloatTree.getValue(openvdb::Coord(0, 0, 1)), tolerance);
    // Average of bg value 1 and set value 4
    CPPUNIT_ASSERT_DOUBLES_EQUAL(2.5, outFloatTree.getValue(openvdb::Coord(0, 1, 0)), tolerance);
    // Average of bg value 1 and bg value 5
    CPPUNIT_ASSERT(outFloatTree.isValueOff(openvdb::Coord(1, 0, 0)));
    CPPUNIT_ASSERT(outFloatTree.isValueOff(openvdb::Coord(0, 1, 2)));
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, outFloatTree.getValue(openvdb::Coord(1, 0, 0)), tolerance);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, outFloatTree.getValue(openvdb::Coord(1000, 1, 2)), tolerance);

    // As above, but combining vector grids:
    const Vec3d one(1, 1, 1), two(2, 2, 2), three(3, 3, 3), four(4, 4, 4), five(5, 5, 5);
    openvdb::Vec3DTree aVecTree(/*bg=*/one), bVecTree(five), outVecTree(one);
    aVecTree.setValue(openvdb::Coord(0, 0, 0), three);
    aVecTree.setValue(openvdb::Coord(0, 0, 1), three);
    bVecTree.setValue(openvdb::Coord(0, 0, 0), -1.0 * one);
    bVecTree.setValue(openvdb::Coord(0, 1, 0), four);
    outVecTree.combine2(aVecTree, bVecTree, Local::vec3dAverage);

    // Average of set value 3 and set value -1
    CPPUNIT_ASSERT(outVecTree.getValue(openvdb::Coord(0, 0, 0)) == one);
    // Average of set value 3 and bg value 5
    CPPUNIT_ASSERT(outVecTree.getValue(openvdb::Coord(0, 0, 1)) == four);
    // Average of bg value 1 and set value 4
    CPPUNIT_ASSERT(outVecTree.getValue(openvdb::Coord(0, 1, 0)) == (2.5 * one));
    // Average of bg value 1 and bg value 5
    CPPUNIT_ASSERT(outVecTree.isValueOff(openvdb::Coord(1, 0, 0)));
    CPPUNIT_ASSERT(outVecTree.isValueOff(openvdb::Coord(0, 1, 2)));
    CPPUNIT_ASSERT(outVecTree.getValue(openvdb::Coord(1, 0, 0)) == three);
    CPPUNIT_ASSERT(outVecTree.getValue(openvdb::Coord(1000, 1, 2)) == three);
}


////////////////////////////////////////


void
TestTreeCombine::testBoolTree()
{
    openvdb::BoolGrid::Ptr sphere = openvdb::BoolGrid::create();

    unittest_util::makeSphere<openvdb::BoolGrid>(/*dim=*/openvdb::Coord(32),
                                                 /*ctr=*/openvdb::Vec3f(0),
                                                 /*radius=*/20.0, *sphere,
                                                 unittest_util::SPHERE_SPARSE_NARROW_BAND);

    openvdb::BoolGrid::Ptr
        aGrid = sphere->copy(),
        bGrid = sphere->copy();

    // CSG operations work only on level sets with a nonzero inside and outside values.
    CPPUNIT_ASSERT_THROW(openvdb::tools::csgUnion(aGrid->tree(), bGrid->tree()),
        openvdb::ValueError);
    CPPUNIT_ASSERT_THROW(openvdb::tools::csgIntersection(aGrid->tree(), bGrid->tree()),
        openvdb::ValueError);
    CPPUNIT_ASSERT_THROW(openvdb::tools::csgDifference(aGrid->tree(), bGrid->tree()),
        openvdb::ValueError);

    openvdb::tools::compSum(aGrid->tree(), bGrid->tree());

    bGrid = sphere->copy();
    openvdb::tools::compMax(aGrid->tree(), bGrid->tree());

    int mismatches = 0;
    openvdb::BoolGrid::ConstAccessor acc = sphere->getConstAccessor();
    for (openvdb::BoolGrid::ValueAllCIter it = aGrid->cbeginValueAll(); it; ++it) {
        if (*it != acc.getValue(it.getCoord())) ++mismatches;
    }
    CPPUNIT_ASSERT_EQUAL(0, mismatches);
}


////////////////////////////////////////


template<typename TreeT>
void
TestTreeCombine::testCompRepl()
{
    typedef typename TreeT::ValueType ValueT;

    const ValueT
        zero = openvdb::zeroVal<ValueT>(),
        minusOne = zero + (-1),
        one = zero + 1,
        three = zero + 3,
        four = zero + 4,
        five = zero + 5;

    {
        TreeT aTree(/*bg=*/one);
        aTree.setValueOn(openvdb::Coord(0, 0, 0), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 1), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 2), aTree.background());
        aTree.setValueOn(openvdb::Coord(0, 1, 2), aTree.background());
        aTree.setValueOff(openvdb::Coord(1, 0, 0), three);
        aTree.setValueOff(openvdb::Coord(1, 0, 1), three);

        TreeT bTree(five);
        bTree.setValueOn(openvdb::Coord(0, 0, 0), minusOne);
        bTree.setValueOn(openvdb::Coord(0, 1, 0), four);
        bTree.setValueOn(openvdb::Coord(0, 1, 2), minusOne);
        bTree.setValueOff(openvdb::Coord(1, 0, 0), minusOne);
        bTree.setValueOff(openvdb::Coord(1, 1, 0), four);

        // Copy active voxels of bTree into aTree.
        openvdb::tools::compReplace(aTree, bTree);

        // a = 3 (On), b = -1 (On)
        CPPUNIT_ASSERT_EQUAL(minusOne, aTree.getValue(openvdb::Coord(0, 0, 0)));

        // a = 3 (On), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(three, aTree.getValue(openvdb::Coord(0, 0, 1)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 0, 1)));

        // a = 1 (On, = bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(one, aTree.getValue(openvdb::Coord(0, 0, 2)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 0, 2)));

        // a = 1 (On, = bg), b = -1 (On)
        CPPUNIT_ASSERT_EQUAL(minusOne, aTree.getValue(openvdb::Coord(0, 1, 2)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 1, 2)));

        // a = 1 (bg), b = 4 (On)
        CPPUNIT_ASSERT_EQUAL(four, aTree.getValue(openvdb::Coord(0, 1, 0)));
        CPPUNIT_ASSERT(aTree.isValueOn(openvdb::Coord(0, 1, 0)));

        // a = 3 (Off), b = -1 (Off)
        CPPUNIT_ASSERT_EQUAL(three, aTree.getValue(openvdb::Coord(1, 0, 0)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1, 0, 0)));

        // a = 3 (Off), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(three, aTree.getValue(openvdb::Coord(1, 0, 1)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1, 0, 1)));

        // a = 1 (bg), b = 4 (Off)
        CPPUNIT_ASSERT_EQUAL(one, aTree.getValue(openvdb::Coord(1, 1, 0)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1, 1, 0)));

        // a = 1 (bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(one, aTree.getValue(openvdb::Coord(1000, 1, 2)));
        CPPUNIT_ASSERT(aTree.isValueOff(openvdb::Coord(1000, 1, 2)));
    }

    // As above, but combining the A grid into the B grid
    {
        TreeT aTree(/*background=*/one);
        aTree.setValueOn(openvdb::Coord(0, 0, 0), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 1), three);
        aTree.setValueOn(openvdb::Coord(0, 0, 2), aTree.background());
        aTree.setValueOn(openvdb::Coord(0, 1, 2), aTree.background());
        aTree.setValueOff(openvdb::Coord(1, 0, 0), three);
        aTree.setValueOff(openvdb::Coord(1, 0, 1), three);

        TreeT bTree(five);
        bTree.setValueOn(openvdb::Coord(0, 0, 0), minusOne);
        bTree.setValueOn(openvdb::Coord(0, 1, 0), four);
        bTree.setValueOn(openvdb::Coord(0, 1, 2), minusOne);
        bTree.setValueOff(openvdb::Coord(1, 0, 0), minusOne);
        bTree.setValueOff(openvdb::Coord(1, 1, 0), four);

        // Copy active voxels of aTree into bTree.
        openvdb::tools::compReplace(bTree, aTree);

        // a = 3 (On), b = -1 (On)
        CPPUNIT_ASSERT_EQUAL(three, bTree.getValue(openvdb::Coord(0, 0, 0)));

        // a = 3 (On), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(three, bTree.getValue(openvdb::Coord(0, 0, 1)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 0, 1)));

        // a = 1 (On, = bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(one, bTree.getValue(openvdb::Coord(0, 0, 2)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 0, 2)));

        // a = 1 (On, = bg), b = -1 (On)
        CPPUNIT_ASSERT_EQUAL(one, bTree.getValue(openvdb::Coord(0, 1, 2)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 1, 2)));

        // a = 1 (bg), b = 4 (On)
        CPPUNIT_ASSERT_EQUAL(four, bTree.getValue(openvdb::Coord(0, 1, 0)));
        CPPUNIT_ASSERT(bTree.isValueOn(openvdb::Coord(0, 1, 0)));

        // a = 3 (Off), b = -1 (Off)
        CPPUNIT_ASSERT_EQUAL(minusOne, bTree.getValue(openvdb::Coord(1, 0, 0)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1, 0, 0)));

        // a = 3 (Off), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(five, bTree.getValue(openvdb::Coord(1, 0, 1)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1, 0, 1)));

        // a = 1 (bg), b = 4 (Off)
        CPPUNIT_ASSERT_EQUAL(four, bTree.getValue(openvdb::Coord(1, 1, 0)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1, 1, 0)));

        // a = 1 (bg), b = 5 (bg)
        CPPUNIT_ASSERT_EQUAL(five, bTree.getValue(openvdb::Coord(1000, 1, 2)));
        CPPUNIT_ASSERT(bTree.isValueOff(openvdb::Coord(1000, 1, 2)));
    }
}


////////////////////////////////////////


void
TestTreeCombine::testCsg()
{
    typedef openvdb::FloatTree TreeT;
    typedef TreeT::Ptr TreePtr;
    typedef openvdb::Grid<TreeT> GridT;

    struct Local {
        static TreePtr readFile(const std::string& fname) {
            std::string filename(fname), gridName("LevelSet");
            size_t space = filename.find_last_of(' ');
            if (space != std::string::npos) {
                gridName = filename.substr(space + 1);
                filename.erase(space);
            }

            TreePtr tree;
            openvdb::io::File file(filename);
            file.open();
            if (openvdb::GridBase::Ptr basePtr = file.readGrid(gridName)) {
                if (GridT::Ptr gridPtr = openvdb::gridPtrCast<GridT>(basePtr)) {
                    tree = gridPtr->treePtr();
                }
            }
            file.close();
            return tree;
        }

        static void writeFile(TreePtr tree, const std::string& filename) {
            openvdb::io::File file(filename);
            openvdb::GridPtrVec grids;
            GridT::Ptr grid = openvdb::createGrid(tree);
            grid->setName("LevelSet");
            grids.push_back(grid);
            file.write(grids);
        }

        static void visitorUnion(TreeT& a, TreeT& b) { openvdb::tools::csgUnion(a, b); }
        static void visitorIntersect(TreeT& a, TreeT& b) { openvdb::tools::csgIntersection(a, b); }
        static void visitorDiff(TreeT& a, TreeT& b) { openvdb::tools::csgDifference(a, b); }
    };

    TreePtr smallTree1, smallTree2, largeTree1, largeTree2, refTree, outTree;

#if TEST_CSG_VERBOSE
    Timer timer;
    timer.start();
#endif

    const std::string testDir("/work/rd/fx_tools/vdb_unittest/TestGridCombine::testCsg/");
    smallTree1 = Local::readFile(testDir + "small1.vdb2 LevelSet");
    CPPUNIT_ASSERT(smallTree1.get() != NULL);
    smallTree2 = Local::readFile(testDir + "small2.vdb2 Cylinder");
    CPPUNIT_ASSERT(smallTree2.get() != NULL);
    largeTree1 = Local::readFile(testDir + "large1.vdb2 LevelSet");
    CPPUNIT_ASSERT(largeTree1.get() != NULL);
    largeTree2 = Local::readFile(testDir + "large2.vdb2 LevelSet");
    CPPUNIT_ASSERT(largeTree2.get() != NULL);

#if TEST_CSG_VERBOSE
    std::cerr << "file read: " << timer.stop() << " sec\n";
#endif

#if TEST_CSG_VERBOSE
    std::cerr << "\n<union>\n";
#endif
    refTree = Local::readFile(testDir + "small_union.vdb2");
    outTree = visitCsg(*smallTree1, *smallTree2, *refTree, Local::visitorUnion);
    //Local::writeFile(outTree, "/tmp/small_union_out.vdb2");
    refTree = Local::readFile(testDir + "large_union.vdb2");
    outTree = visitCsg(*largeTree1, *largeTree2, *refTree, Local::visitorUnion);
    //Local::writeFile(outTree, "/tmp/large_union_out.vdb2");

#if TEST_CSG_VERBOSE
    std::cerr << "\n<intersection>\n";
#endif
    refTree = Local::readFile(testDir + "small_intersection.vdb2");
    outTree = visitCsg(*smallTree1, *smallTree2, *refTree, Local::visitorIntersect);
    //Local::writeFile(outTree, "/tmp/small_intersection_out.vdb2");
    refTree = Local::readFile(testDir + "large_intersection.vdb2");
    outTree = visitCsg(*largeTree1, *largeTree2, *refTree, Local::visitorIntersect);
    //Local::writeFile(outTree, "/tmp/large_intersection_out.vdb2");

#if TEST_CSG_VERBOSE
    std::cerr << "\n<difference>\n";
#endif
    refTree = Local::readFile(testDir + "small_difference.vdb2");
    outTree = visitCsg(*smallTree1, *smallTree2, *refTree, Local::visitorDiff);
    //Local::writeFile(outTree, "/tmp/small_difference_out.vdb2");
    refTree = Local::readFile(testDir + "large_difference.vdb2");
    outTree = visitCsg(*largeTree1, *largeTree2, *refTree, Local::visitorDiff);
    //Local::writeFile(outTree, "/tmp/large_difference_out.vdb2");
}


template<typename TreeT, typename VisitorT>
typename TreeT::Ptr
TestTreeCombine::visitCsg(const TreeT& aInputTree, const TreeT& bInputTree,
    const TreeT& refTree, const VisitorT& visitor)
{
    typedef typename TreeT::Ptr TreePtr;

#if TEST_CSG_VERBOSE
    Timer timer;

    timer.start();
#endif
    TreePtr aTree(new TreeT(aInputTree));
    TreeT bTree(bInputTree);
#if TEST_CSG_VERBOSE
    std::cerr << "deep copy: " << timer.stop() << " sec\n";
#endif

#if (TEST_CSG_VERBOSE > 1)
    std::cerr << "\nA grid:\n";
    aTree->print(std::cerr, /*verbose=*/3);
    std::cerr << "\nB grid:\n";
    bTree.print(std::cerr, /*verbose=*/3);
    std::cerr << "\nExpected:\n";
    refTree.print(std::cerr, /*verbose=*/3);
    std::cerr << "\n";
#endif

    // Compute the CSG combination of the two grids.
#if TEST_CSG_VERBOSE
    timer.start();
#endif
    visitor(*aTree, bTree);
#if TEST_CSG_VERBOSE
    std::cerr << "combine: " << timer.stop() << " sec\n";
#endif
#if (TEST_CSG_VERBOSE > 1)
    std::cerr << "\nActual:\n";
    aTree->print(std::cerr, /*verbose=*/3);
#endif

    std::ostringstream aInfo, refInfo;
    aTree->print(aInfo, /*verbose=*/3);
    refTree.print(refInfo, /*verbose=*/3);

    CPPUNIT_ASSERT_EQUAL(refInfo.str(), aInfo.str());

    CPPUNIT_ASSERT(aTree->hasSameTopology(refTree));

    return aTree;
}

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
