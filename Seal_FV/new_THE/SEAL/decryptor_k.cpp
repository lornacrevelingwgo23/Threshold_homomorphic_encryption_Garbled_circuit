#include <algorithm>
#include <stdexcept>
#include <cmath>
#include "Decryptor_k.h"
#include "util/common.h"
#include "util/uintcore.h"
#include "util/uintarith.h"
#include "util/polycore.h"
#include "util/polyarith.h"
#include "util/polyarithmod.h"
#include "util/clipnormal.h"
#include "util/randomtostd.h"
#include "bigpolyarith.h"
#include "bigpoly.h"
#include "biguint.h"
#include "util/uintarithmod.h"
#include "util/polyextras.h"
#include "polycrt.h"
#include "keygenerator.h"

using namespace std;
using namespace seal::util;

namespace seal
{
    namespace
    {
        bool are_poly_coefficients_less_than(const BigPoly &poly, const BigUInt &max_coeff)
        {
            return util::are_poly_coefficients_less_than(poly.pointer(), poly.coeff_count(), poly.coeff_uint64_count(), max_coeff.pointer(), max_coeff.uint64_count());
        }
    }
    
    Decryptor_k::Decryptor_k(const EncryptionParameters &parms, const BigPoly &secret_key) :
    poly_modulus_(parms.poly_modulus()), coeff_modulus_(parms.coeff_modulus()), plain_modulus_(parms.plain_modulus()), secret_key_(secret_key), orig_plain_modulus_bit_count_(parms.plain_modulus().significant_bit_count())
    {
        // Verify required parameters are non-zero and non-nullptr.
        if (poly_modulus_.is_zero())
        {
            throw invalid_argument("poly_modulus cannot be zero");
        }
        if (coeff_modulus_.is_zero())
        {
            throw invalid_argument("coeff_modulus cannot be zero");
        }
        if (plain_modulus_.is_zero())
        {
            throw invalid_argument("plain_modulus cannot be zero");
        }
        
        if (secret_key_.is_zero())
        {
            throw invalid_argument("secret_key cannot be zero");
        }
        
        // Verify parameters.
        if (plain_modulus_ >= coeff_modulus_)
        {
            throw invalid_argument("plain_modulus must be smaller than coeff_modulus");
        }
        if (!are_poly_coefficients_less_than(poly_modulus_, coeff_modulus_))
        {
            throw invalid_argument("poly_modulus cannot have coefficients larger than coeff_modulus");
        }
        
        // Resize encryption parameters to consistent size.
        int coeff_count = poly_modulus_.significant_coeff_count();
        int coeff_bit_count = coeff_modulus_.significant_bit_count();
        int coeff_uint64_count = divide_round_up(coeff_bit_count, bits_per_uint64);
        if (poly_modulus_.coeff_count() != coeff_count || poly_modulus_.coeff_bit_count() != coeff_bit_count)
        {
            poly_modulus_.resize(coeff_count, coeff_bit_count);
        }
        if (coeff_modulus_.bit_count() != coeff_bit_count)
        {
            coeff_modulus_.resize(coeff_bit_count);
        }
        if (plain_modulus_.bit_count() != coeff_bit_count)
        {
            plain_modulus_.resize(coeff_bit_count);
        }
        if (secret_key_.coeff_count() != coeff_count || secret_key_.coeff_bit_count() != coeff_bit_count ||
            secret_key_.significant_coeff_count() == coeff_count || !are_poly_coefficients_less_than(secret_key_, coeff_modulus_))
        {
            throw invalid_argument("secret_key is not valid for encryption parameters");
        }
        
        // Set the secret_key_array to have size 1 (first power of secret)
        secret_key_array_.resize(1, coeff_count, coeff_bit_count);
        set_poly_poly(secret_key_.pointer(), coeff_count, coeff_uint64_count, secret_key_array_.pointer(0));
        
        MemoryPool &pool = *MemoryPool::default_pool();
        
        // Calculate coeff_modulus / plain_modulus.
        coeff_div_plain_modulus_.resize(coeff_bit_count);
        Pointer temp(allocate_uint(coeff_uint64_count, pool));
        divide_uint_uint(coeff_modulus_.pointer(), plain_modulus_.pointer(), coeff_uint64_count, coeff_div_plain_modulus_.pointer(), temp.get(), pool);
        
        // Calculate coeff_modulus / plain_modulus / 2.
        coeff_div_plain_modulus_div_two_.resize(coeff_bit_count);
        right_shift_uint(coeff_div_plain_modulus_.pointer(), 1, coeff_uint64_count, coeff_div_plain_modulus_div_two_.pointer());
        
        // Calculate coeff_modulus / 2.
        upper_half_threshold_.resize(coeff_bit_count);
        half_round_up_uint(coeff_modulus_.pointer(), coeff_uint64_count, upper_half_threshold_.pointer());
        
        // Calculate upper_half_increment.
        upper_half_increment_.resize(coeff_bit_count);
        multiply_truncate_uint_uint(plain_modulus_.pointer(), coeff_div_plain_modulus_.pointer(), coeff_uint64_count, upper_half_increment_.pointer());
        sub_uint_uint(coeff_modulus_.pointer(), upper_half_increment_.pointer(), coeff_uint64_count, upper_half_increment_.pointer());
        
        // Initialize moduli.
        polymod_ = PolyModulus(poly_modulus_.pointer(), coeff_count, coeff_uint64_count);
        mod_ = Modulus(coeff_modulus_.pointer(), coeff_uint64_count, pool);
        
        //Generating the secret key array for MU.
        int cp_size_minus_1=1;
        KeyGenerator generatorMU(parms);
        generatorMU.generate();
        cout << "... MU generation complete" << endl;
        secret_key_MU_array.resize(cp_size_minus_1, coeff_count, coeff_bit_count);
        for (int i=0; i<cp_size_minus_1; i++) {
            generatorMU.generate();
            BigPoly tmp= generatorMU.secret_key();
            set_poly_poly(tmp.pointer(), coeff_count, coeff_uint64_count, secret_key_MU_array.pointer(i));
        }
        
        // Make sure we have enough secret keys computed
        compute_secret_key_array(cp_size_minus_1);
        
        //BigPolyArray secret_key_SPU_array;
        secret_key_SPU_array.resize(secret_key_array_.size(), coeff_count, coeff_bit_count);
        cout<<"secret_key_array_.size is: "<<secret_key_array_.size()<<"secret_key_MU_array.size is: "<<secret_key_MU_array.size()<<endl;
        cout<<"coeff_count is:"<<coeff_count<<endl;
        cout<<"coeff_uint64_count is:"<<coeff_uint64_count<<endl;
        for (int i=0; i<secret_key_array_.size(); i++) {
            sub_poly_poly_coeffmod(secret_key_array_.pointer(i), secret_key_MU_array.pointer(i), coeff_count, coeff_modulus_.pointer(), coeff_uint64_count, secret_key_SPU_array.pointer(i));
        }
        
        
        ////////////////////////////////////
        //For the testing of correct splitting of the secret keys
        ////////////////////////////////////
        
        BigPoly destination1;
        
        if (destination1.coeff_count() != coeff_count || destination1.coeff_bit_count() != coeff_bit_count)
        {
            destination1.resize(coeff_count, coeff_bit_count);
        }
        
        add_poly_poly_coeffmod(secret_key_MU_array[0].pointer(), secret_key_SPU_array[0].pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination1.pointer());
        if (secret_key_array_[0]==destination1) {
            cout<<"key split correctly"<<endl;
        }
        
        cout<<"secret_key_SPU_array size is: "<<secret_key_SPU_array.size()<<endl;
    }
    
    
    void Decryptor_k::decryptSPU(BigPolyArray &encrypted, BigPolyArray & plaintext_slot_noise, BigPoly &destination, int vector_size)
    {
        // Extract encryption parameters.
        // Remark: poly_modulus_ has enlarged coefficient size set in constructor
        int coeff_count = poly_modulus_.coeff_count();
        int coeff_bit_count = poly_modulus_.coeff_bit_count();
        int coeff_uint64_count = divide_round_up(coeff_bit_count, bits_per_uint64);
        
        // Verify parameters.
        if (encrypted.size() < 2 || encrypted.coeff_count() != coeff_count || encrypted.coeff_bit_count() != coeff_bit_count)
        {
            throw invalid_argument("encrypted is not valid for encryption parameters");
        }
        
#ifdef _DEBUG
        for (int i = 0; i < encrypted.size(); ++i)
        {
            if (encrypted[i].significant_coeff_count() == coeff_count || !are_poly_coefficients_less_than(encrypted[i], coeff_modulus_))
            {
                throw invalid_argument("encrypted is not valid for encryption parameters");
            }
        }
#endif
        
        // Make sure destination is of right size to perform all computations. At the end we will
        // resize the coefficients to be the size of plain_modulus.
        // Remark: plain_modulus_ has enlarged coefficient size set in constructor
        if (destination.coeff_count() != coeff_count || destination.coeff_bit_count() != coeff_bit_count)
        {
            destination.resize(coeff_count, coeff_bit_count);
        }
        
        MemoryPool &pool = *MemoryPool::default_pool();
        
        BigPoly destination2;
        
        if (destination2.coeff_count() != coeff_count || destination2.coeff_bit_count() != coeff_bit_count)
        {
            destination2.resize(coeff_count, coeff_bit_count);
        }
        BigPoly temp3;
        
        if (temp3.coeff_count() != coeff_count || temp3.coeff_bit_count() != coeff_bit_count)
        {
            temp3.resize(coeff_count, coeff_bit_count);
        }
        //decrypting the second part of the ciphextext using secret key of MU.
        dot_product_bigpolyarray_polymod_coeffmod(encrypted.pointer(1), secret_key_MU_array.pointer(0), 1, polymod_, mod_, temp3.pointer(), pool);
        BigPoly tp3=temp3;
        //Enabling the aggregation of all the slot plaintext values in the first slot.
        for (int i=0; i<vector_size; i++) {
            temp3=tp3;
            polyPermutate(temp3, 2*i+1, poly_modulus_.coeff_count()-1);
            add_poly_poly_coeffmod(destination2.pointer(), temp3.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        }
        
        BigPoly temp5;
        
        if (temp5.coeff_count() != coeff_count || temp5.coeff_bit_count() != coeff_bit_count)
        {
            temp5.resize(coeff_count, coeff_bit_count);
        }
        //generating and adding the plaintext slot noise.
        dot_product_bigpolyarray_polymod_coeffmod(plaintext_slot_noise.pointer(1), secret_key_MU_array.pointer(0), 1, polymod_, mod_, temp5.pointer(), pool);
        add_poly_poly_coeffmod(destination2.pointer(), temp5.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        destination=destination2;
        
        //generating and adding the final noise for the partial ciphertext from SPU.
        BigPoly errorSPU(coeff_count, coeff_bit_count);
        errorSPU.set_zero();
        unique_ptr<UniformRandomGenerator> randomSPU(UniformRandomGeneratorFactory::default_factory()->create());
        Pointer tempSPU(allocate_poly(coeff_count, coeff_uint64_count, pool));
        set_poly_coeffs_normal(tempSPU.get(), randomSPU.get());
        add_poly_poly_coeffmod(tempSPU.get(), errorSPU.pointer(), coeff_count, mod_.get(), coeff_uint64_count, errorSPU.pointer());
        add_poly_poly_coeffmod(destination.pointer(), errorSPU.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination.pointer());
    }
    
    void Decryptor_k::decryptMU(BigPolyArray &encrypted, BigPolyArray & plaintext_slot_noise, BigPoly &destination, BigPoly &cpSPU, int vector_size)
    {
        //cout<<"secret_key_SPU_array size is: "<<secret_key_SPU_array.size()<<endl;
        // Extract encryption parameters.
        // Remark: poly_modulus_ has enlarged coefficient size set in constructor
        int coeff_count = poly_modulus_.coeff_count();
        int coeff_bit_count = poly_modulus_.coeff_bit_count();
        int coeff_uint64_count = divide_round_up(coeff_bit_count, bits_per_uint64);
        
        // Verify parameters.
        if (encrypted.size() < 2 || encrypted.coeff_count() != coeff_count || encrypted.coeff_bit_count() != coeff_bit_count)
        {
            throw invalid_argument("encrypted is not valid for encryption parameters");
        }
        
#ifdef _DEBUG
        for (int i = 0; i < encrypted.size(); ++i)
        {
            if (encrypted[i].significant_coeff_count() == coeff_count || !are_poly_coefficients_less_than(encrypted[i], coeff_modulus_))
            {
                throw invalid_argument("encrypted is not valid for encryption parameters");
            }
        }
#endif
        
        // Verify parameters.
        if (plaintext_slot_noise.size() < 2 || plaintext_slot_noise.coeff_count() != coeff_count || plaintext_slot_noise.coeff_bit_count() != coeff_bit_count)
        {
            throw invalid_argument("plaintext_slot_noise is not valid for encryption parameters");
        }
        
#ifdef _DEBUG
        for (int i = 0; i < plaintext_slot_noise.size(); ++i)
        {
            if (plaintext_slot_noise[i].significant_coeff_count() == coeff_count || !are_poly_coefficients_less_than(plaintext_slot_noise[i], coeff_modulus_))
            {
                throw invalid_argument("plaintext_slot_noise is not valid for encryption parameters");
            }
        }
#endif
        
        // Make sure destination is of right size to perform all computations. At the end we will
        // resize the coefficients to be the size of plain_modulus.
        // Remark: plain_modulus_ has enlarged coefficient size set in constructor
        
        MemoryPool &pool = *MemoryPool::default_pool();
        
        /*
         Firstly find c_0 + c_1 *s + ... + c_{count-1} * s^{count-1} mod q
         This is equal to Delta m + v where ||v|| < Delta/2.
         So, add Delta / 2 and now we have something which is Delta * (m + epsilon) where epsilon < 1
         Therefore, we can (integer) divide by Delta and the answer will round down to m.
         */
        
        if (destination.coeff_count() != coeff_count || destination.coeff_bit_count() != coeff_bit_count)
        {
            destination.resize(coeff_count, coeff_bit_count);
        }
        
        
        cout<<"1111"<<endl;
        
        BigPoly destination2;
        
        if (destination2.coeff_count() != coeff_count || destination2.coeff_bit_count() != coeff_bit_count)
        {
            destination2.resize(coeff_count, coeff_bit_count);
        }
        
        BigPoly temp4;
        if (temp4.coeff_count() != coeff_count || temp4.coeff_bit_count() != coeff_bit_count)
        {
            temp4.resize(coeff_count, coeff_bit_count);
        }
        //Decrypting the first part of ciphertext with SPU's secret key.
        dot_product_bigpolyarray_polymod_coeffmod(encrypted.pointer(1), secret_key_SPU_array.pointer(0), 1, polymod_, mod_, temp4.pointer(), pool);
        BigPoly tp4=temp4;
        //Enabling the aggregation of all the slot plaintext values in the first slot.
        for (int i=0; i<vector_size; i++) {
            temp4=tp4;
            polyPermutate(temp4, 2*i+1, poly_modulus_.coeff_count()-1);
            add_poly_poly_coeffmod(destination2.pointer(), temp4.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        }
        
        BigPoly temp0;
        
        if (temp0.coeff_count() != coeff_count || temp0.coeff_bit_count() != coeff_bit_count)
        {
            temp0.resize(coeff_count, coeff_bit_count);
        }
        //Transforming the first part of the cipertext for the aggregation of the plaintext slots in the first plaintext slot. Then add the transformed first part of the ciphertext to the final result.
        BigPoly e0=encrypted[0];
        //Enabling the aggregation of all the slot plaintext values in the first slot.
        for (int i=0; i<vector_size; i++) {
            temp0=e0;
            polyPermutate(temp0, 2*i+1, poly_modulus_.coeff_count()-1);
            add_poly_poly_coeffmod(destination2.pointer(), temp0.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        }
        
        
        BigPoly temp6;
        
        if (temp6.coeff_count() != coeff_count || temp6.coeff_bit_count() != coeff_bit_count)
        {
            temp6.resize(coeff_count, coeff_bit_count);
        }
        //Decrypting and adding the plaintext slot noise for the slots other than the first one.
        dot_product_bigpolyarray_polymod_coeffmod(plaintext_slot_noise.pointer(1), secret_key_SPU_array.pointer(0), 1, polymod_, mod_, temp6.pointer(), pool);
        add_poly_poly_coeffmod(destination2.pointer(), temp6.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        add_poly_poly_coeffmod(destination2.pointer(), plaintext_slot_noise.pointer(0), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        //Adding the partial decryption result from SPU.
        add_poly_poly_coeffmod(destination2.pointer(), cpSPU.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination2.pointer());
        
        destination=destination2;
        
        ////////////////////////////////////
        //Add noise to mask the final result.
        ////////////////////////////////////
        
        MemoryPool &poolforrMU = *MemoryPool::default_pool();
        BigPoly errorMU(coeff_count, coeff_bit_count);
        errorMU.set_zero();
        unique_ptr<UniformRandomGenerator> randomMU(UniformRandomGeneratorFactory::default_factory()->create());
        Pointer tempMU(allocate_poly(coeff_count, coeff_uint64_count, poolforrMU));
        set_poly_coeffs_normal(tempMU.get(), randomMU.get());
        add_poly_poly_coeffmod(tempMU.get(), errorMU.pointer(), coeff_count, mod_.get(), coeff_uint64_count, errorMU.pointer());
        add_poly_poly_coeffmod(destination.pointer(), errorMU.pointer(), coeff_count, mod_.get(), coeff_uint64_count, destination.pointer());
        
        
        
        // For each coefficient, reposition and divide by coeff_div_plain_modulus.
        uint64_t *dest_coeff = destination.pointer();
        Pointer quotient(allocate_uint(coeff_uint64_count, pool));
        Pointer big_alloc(allocate_uint(2 * coeff_uint64_count, pool));
        for (int i = 0; i < coeff_count; ++i)
        {
            // Round to closest level by adding coeff_div_plain_modulus_div_two (mod coeff_modulus).
            add_uint_uint_mod(dest_coeff, coeff_div_plain_modulus_div_two_.pointer(), coeff_modulus_.pointer(), coeff_uint64_count, dest_coeff);
            
            // Reposition if it is in upper-half of coeff_modulus.
            bool is_upper_half = is_greater_than_or_equal_uint_uint(dest_coeff, upper_half_threshold_.pointer(), coeff_uint64_count);
            if (is_upper_half)
            {
                sub_uint_uint(dest_coeff, upper_half_increment_.pointer(), coeff_uint64_count, dest_coeff);
            }
            
            // Find closest level.
            divide_uint_uint_inplace(dest_coeff, coeff_div_plain_modulus_.pointer(), coeff_uint64_count, quotient.get(), pool, big_alloc.get());
            set_uint_uint(quotient.get(), coeff_uint64_count, dest_coeff);
            dest_coeff += coeff_uint64_count;
        }
        
        // Resize the coefficient to the original plain_modulus size
        destination.resize(coeff_count, orig_plain_modulus_bit_count_);
        cout<<"decryption combine donedecryption combine donedecryption combine done"<<endl;
    }
    
    //This function aims to perform the following operation: given an input polynomial F(x) in R_q[x]/(x^m+1), and an odd integer number exponent
    //(it has to be odd to guarantee that it is co-prime with m), it will modify F(x) to be F(x^exponent) in the underlying ring.
    void Decryptor_k::polyPermutate(BigPoly& inp, int exponent, int poly_mod)
    {
        BigPoly res=inp;
        
        int coeff_count = poly_modulus_.coeff_count();
        int coeff_bit_count = poly_modulus_.coeff_bit_count();
        
        int *power = new int[coeff_count-1];
        int *tmp = new int[coeff_count-1];
        for (int i=0; i<coeff_count-1; i++) {
            int tmp2=(i*exponent)%poly_mod;
            tmp[i]=(i*exponent/poly_mod)%2;
            power[tmp2]=i;
        }
        
        for (int i=0; i<coeff_count-1; i++) {
            BigUInt t;
            t.set_zero();
            if (tmp[power[i]]) {
                res[i]=t.operator-(inp[power[i]]);
            }
            else {
                res[i]=t.operator+(inp[power[i]]);
            }
        }
        inp=res;
        return;
    }
    
    void Decryptor_k::compute_secret_key_array(int max_power)
    {
        if (max_power < 1)
        {
            throw invalid_argument("max_power cannot be less than 1");
        }
        
        int old_count = secret_key_array_.size();
        int new_count = max(max_power, secret_key_array_.size());
        
        if (old_count == new_count)
        {
            return;
        }
        
        int coeff_count = poly_modulus_.coeff_count();
        int coeff_bit_count = coeff_modulus_.bit_count();
        int coeff_uint64_count = divide_round_up(coeff_bit_count, bits_per_uint64);
        
        // Compute powers of secret key until max_power
        secret_key_array_.resize(new_count, coeff_count, coeff_bit_count);
        
        MemoryPool &pool = *MemoryPool::default_pool();
        
        int poly_ptr_increment = coeff_count * coeff_uint64_count;
        uint64_t *prev_poly_ptr = secret_key_array_.pointer(old_count - 1);
        uint64_t *next_poly_ptr = prev_poly_ptr + poly_ptr_increment;
        for (int i = old_count; i < new_count; ++i)
        {
            multiply_poly_poly_polymod_coeffmod(prev_poly_ptr, secret_key_.pointer(), polymod_, mod_, next_poly_ptr, pool);
            prev_poly_ptr = next_poly_ptr;
            next_poly_ptr += poly_ptr_increment;
        }
    }
    //This function aims to generate normal noise. 
    void Decryptor_k::set_poly_coeffs_normal(std::uint64_t *poly, UniformRandomGenerator *random) const
    {
        int coeff_count = poly_modulus_.coeff_count();
        int coeff_bit_count = poly_modulus_.coeff_bit_count();
        int coeff_uint64_count = divide_round_up(coeff_bit_count, bits_per_uint64);
        if (noise_standard_deviation_ == 0 || noise_max_deviation_ == 0)
        {
            set_zero_poly(coeff_count, coeff_uint64_count, poly);
            return;
        }
        RandomToStandardAdapter engine(random);
        ClippedNormalDistribution dist(0, noise_standard_deviation_, noise_max_deviation_);
        for (int i = 0; i < coeff_count - 1; ++i)
        {
            int64_t noise = static_cast<int64_t>(dist(engine));
            if (noise > 0)
            {
                set_uint(static_cast<uint64_t>(noise), coeff_uint64_count, poly);
            }
            else if (noise < 0)
            {
                noise = -noise;
                set_uint(static_cast<uint64_t>(noise), coeff_uint64_count, poly);
                sub_uint_uint(coeff_modulus_.pointer(), poly, coeff_uint64_count, poly);
            }
            else
            {
                set_zero_uint(coeff_uint64_count, poly);
            }
            poly += coeff_uint64_count;
        }
        set_zero_uint(coeff_uint64_count, poly);
    }
}