#ifndef PROOF_CHECKER_H
#define PROOF_CHECKER_H

#ifdef __cplusplus
extern "C" {
#endif

// Verify a proof given as a single string (lines separated by '\n').
// On success: returns 0 (valid proof) or 1 (invalid proof).
// On error (parse/internal): returns negative values.
// The function allocates an output string with malloc and stores it into *output.
// Caller must call free_output(*output) (or free) when done.
int verify_proof(const char *input, char **output);

// Free an output string returned by verify_proof.
void free_output(char *p);

#ifdef __cplusplus
}
#endif

#endif // PROOF_CHECKER_H

