from google import genai
import ctypes
import os



def proof_generator(premises, goal):
    client = genai.Client(api_key="Enter your Gemeni API Key here")
    prompt = """You are a professional proof solver for propositional logic using only the Lukasiewicz-Church (P2) axiom system.
Use Polish prefix notation with:
  c  = implication
  n  = negation
Propositional metavariables: A, B, C (use uppercase letters for atoms).

Axiom Schemas (compact, no arrows):
AX1: cAcBA
AX2: ccAcBCccABcAC
AX3: ccnBnAcAB

Rule of Inference:
  MP i j    (Modus Ponens: from lines i and j where line j is c<phi><psi>, infer psi)

Format for output (each line exactly):
  <line-number> <formula> <justification>
Justifications allowed: Premise, AX1, AX2, AX3, MP i j

Example 1:
Premises: cPQ, P
Goal: Q
Proof:
1 cPQ Premise
2 P Premise
3 Q MP 2 1

Example 2:
Premises: cPcQP
Goal: cQP
Proof:
1 cPcQP Premise
2 cQP MP 1 1

Now, given the premises and goal below, generate a numbered proof using only the above axioms and MP. Also note that don't use any parenthesis
Premises: {premises}
Goal: {goal}
Proof:
""".format(premises=", ".join(premises), goal=goal)
    response = client.models.generate_content(
        model="gemini-2.5-flash",
        contents=prompt,
    )
    return response.text


# --- Proof Verifier Integration ---
libpath = os.path.join(os.path.dirname(__file__), "libproofchecker.so")
lib = ctypes.CDLL(libpath)
lib.verify_proof.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
lib.verify_proof.restype = ctypes.c_int
lib.free_output.argtypes = [ctypes.c_char_p]
lib.free_output.restype = None

def verify_proof(proof_str: str):
    out_ptr = ctypes.c_char_p()
    rc = lib.verify_proof(proof_str.encode('utf-8'), ctypes.byref(out_ptr))
    output = out_ptr.value.decode('utf-8') if out_ptr.value else ''
    if out_ptr:
        lib.free_output(out_ptr)
    return rc, output

if __name__ == "__main__":
  # Example usage: generate and verify proof
  premises = ["cPZ","ccPZZ","cZQ", "P"]
  goal = "ccPZcccPQZZ"
  proof = proof_generator(premises, goal)
  print("Generated Proof:\n", proof)
  rc, out = verify_proof(proof)
  print("\nVerifier Return code:", rc)
  print("Verifier Output:")
  print(out)
