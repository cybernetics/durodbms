package net.sf.duro.tests;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import net.sf.duro.DException;
import net.sf.duro.DSession;
import net.sf.duro.Tuple;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

public class TestExecute {

    private DSession session;

    @Before
    public void setUp() throws Exception {
        session = DSession.createSession();
    }

    @After
    public void tearDown() throws Exception {
        session.close();
    }

    @Test
    public void testInteger() {
        session.execute("var n init 343;");
        assertEquals(Integer.valueOf(343), session.evaluate("n"));
    }

    @Test
    public void testString() {
        session.execute("var s string init 'Crystal';");
        assertEquals("Crystal", session.evaluate("s"));
    }

    @Test
    public void testBoolean() {
        session.execute("var b boolean;" + "b:= true;");
        assertEquals(Boolean.TRUE, session.evaluate("b"));
    }

    @Test
    public void testFloat() {
        session.execute("var f float;" + "f := 7.8;");
        assertEquals(Double.valueOf(7.8), session.evaluate("f"));
    }

    @Test
    public void testBinary() {
        session.execute("var bin binary init X'01f0';");
        assertArrayEquals(new byte[] { (byte) 1, (byte) 0xf0 },
                (byte[]) session.evaluate("bin"));
    }

    @Test
    public void testTuple() {
        session.execute("var t tuple { a string };"
                + "t := tuple { a 'Casa blanca' };");

        Tuple t = new Tuple();
        t.setAttribute("a", "Casa blanca");
        assertEquals(t, session.evaluate("t"));
    }

}
