package net.sf.duro;

import java.util.HashMap;
import java.util.Map;

/**
 * Instances of this class represent a DuroDBMS scalar type.
 * 
 * @author Rene Hartmann
 *
 */
public class ScalarType extends Type {

    private static final long serialVersionUID = 1L;

    private static Map<String, ScalarType> typeMap = new HashMap<String, ScalarType>();

    /** Represents the DuroDBMS type <code>boolean</code>. */
    public static final ScalarType BOOLEAN = new ScalarType("boolean", null);

    /** Represents the DuroDBMS type <code>integer</code>. */
    public static final ScalarType INTEGER = new ScalarType("integer", null);

    /** Represents the DuroDBMS type <code>string</code>. */
    public static final ScalarType STRING = new ScalarType("string", null);
    
    /** Represents the DuroDBMS type <code>float</code>. */
    public static final ScalarType FLOAT = new ScalarType("float", null);

    /** Represents the DuroDBMS type <code>binary</code>. */    
    public static final ScalarType BINARY = new ScalarType("binary", null);

    private final String name;
    private final Possrep[] possreps;

    static {
        typeMap.put("boolean", BOOLEAN);
        typeMap.put("integer", INTEGER);
        typeMap.put("string", STRING);
        typeMap.put("float", FLOAT);
        typeMap.put("binary", BINARY);
    }

    protected ScalarType(String name, Possrep[] possreps) {
        this.name = name;
        this.possreps = possreps;
    }

    private static native Possrep[] typePossreps(String name, DSession session);

    @Override
    public String getName() {
        return name;
    }

    @Override
    public boolean isScalar() {
        return true;
    }

    /**
     * Returns the possible representations of this type.
     *  
     * @return an array which contains the possible representations.
     */
    public Possrep[] getPossreps() {
        return possreps;
    }

    /**
     * Returns an instance of ScalarType which represents the DuroDBMS type
     * <code>typename</code>.
     * 
     * @param typename the name of the DuroDBMS type
     * @param session   the DuroDBMS session
     * @return the ScalarType instance representing the type
     */
    public static ScalarType fromString(String typename, DSession session) {
        ScalarType type = typeMap.get(typename);
        if (type == null) {
            Possrep[] possreps = typePossreps(typename, session);
            if (possreps == null)
                return null;

            type = new ScalarType(typename, possreps);
            typeMap.put(typename, type);
        }
        return type;
    }

    public static ScalarType fromString(String typename) {
        return typeMap.get(typename);
    }
}
