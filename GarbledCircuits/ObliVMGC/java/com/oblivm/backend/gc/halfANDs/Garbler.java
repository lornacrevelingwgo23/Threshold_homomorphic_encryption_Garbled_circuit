package com.oblivm.backend.gc.halfANDs;

import java.nio.ByteBuffer;
import java.security.MessageDigest;

import com.oblivm.backend.gc.GCSignal;

final class Garbler {
	private MessageDigest sha1 = null;
	Garbler() {
        try {
            sha1 = MessageDigest.getInstance("SHA-1");
        }
        catch (Exception e) {
            e.printStackTrace();
            System.exit(1);
        }
    }
	
	ByteBuffer buffer = ByteBuffer.allocate(GCSignal.len+9); 
	public GCSignal hash(GCSignal lb, long k, boolean b) {
		buffer.clear();
		sha1.update(buffer.put(lb.bytes).putLong(k).put(b?(byte)1:(byte)0));
		return GCSignal.newInstance(sha1.digest());
	}
}