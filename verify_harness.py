# verify_harness.py
import ctypes
import os

libpath = os.path.join(os.path.dirname(__file__), "libproofchecker.so")
lib = ctypes.CDLL(libpath)

# Signatures
lib.verify_proof.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
lib.verify_proof.restype = ctypes.c_int
lib.free_output.argtypes = [ctypes.c_char_p]
lib.free_output.restype = None

def verify_proof(proof_str: str):
    out_ptr = ctypes.c_char_p()
    rc = lib.verify_proof(proof_str.encode('utf-8'), ctypes.byref(out_ptr))
    output = out_ptr.value.decode('utf-8') if out_ptr.value else ''
    # free the C-allocated buffer
    if out_ptr:
        lib.free_output(out_ptr)
    return rc, output

if __name__ == "__main__":
    # example proof (simple demonstration)
    proof = """1 cPcQP AX1
2 P Premise
3 cQP MP 2 1
"""
    rc, out = verify_proof(proof)
    print("Return code:", rc)
    print("Checker output:")
    print(out)

