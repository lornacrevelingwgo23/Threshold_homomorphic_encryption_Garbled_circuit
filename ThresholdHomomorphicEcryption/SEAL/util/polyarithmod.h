#ifndef SEAL_UTIL_POLYARITHMOD_H
#define SEAL_UTIL_POLYARITHMOD_H

#include <cstdint>
#include "util/modulus.h"
#include "util/mempool.h"
#include "util/polymodulus.h"

namespace seal
{
    namespace util
    {
        void modulo_poly_inplace(std::uint64_t *value, int value_coeff_count, const PolyModulus &poly_modulus, const Modulus &modulus, MemoryPool &pool);

        void modulo_poly(const std::uint64_t *value, int value_coeff_count, const PolyModulus &poly_modulus, const Modulus &modulus, std::uint64_t *result, MemoryPool &pool);

        void multiply_poly_poly_polymod_coeffmod(const std::uint64_t *operand1, const std::uint64_t *operand2, const PolyModulus &poly_modulus, const Modulus &modulus, std::uint64_t *result, MemoryPool &pool);

        void multiply_poly_poly_polymod_coeffmod_inplace(const std::uint64_t *operand1, const std::uint64_t *operand2, const PolyModulus &poly_modulus, const Modulus &modulus, std::uint64_t *result, MemoryPool &pool);

        void nonfftmultiply_poly_poly_polymod_coeffmod(const std::uint64_t *operand1, const std::uint64_t *operand2, const PolyModulus &poly_modulus, const Modulus &modulus, std::uint64_t *result, MemoryPool &pool);

        void nonfftmultiply_poly_poly_polymod_coeffmod_inplace(const std::uint64_t *operand1, const std::uint64_t *operand2, const PolyModulus &poly_modulus, const Modulus &modulus, std::uint64_t *result, MemoryPool &pool);

        bool try_invert_poly_coeffmod(const std::uint64_t *operand, const std::uint64_t *poly_modulus, int coeff_count, const Modulus &modulus, std::uint64_t *result, MemoryPool &pool);
    }
}
#endif // SEAL_UTIL_POLYARITHMOD_H
