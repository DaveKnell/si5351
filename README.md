# si5351
A SI5351 driver.

## Interesting notes on the SI5351

One of the interesting things about the SI5351 is its PLL dividers which have both an integer and a fractional part - a + b/c as it is in the documentation, with c from 1 to 1,048,575.  The code that I've seen which calculates the fractional part just sets c to 1,048,575, but this isn't necessarily optimal. 

Consider where b/c should be 1/7 - 1/7 can be precisely expressed as 1/7, but it'd be 149,796.429.../1,048,575.

So how to calculate the most accurate combination of numerator and denominator?  A straighforward method is to calculate the best approximation for each denominator and return the optimum one found:

    def simple_approx(x, max_denominator=1000):
        # Ensure x is between 0 and 1
        if not 0 <= x <= 1:
            raise ValueError("x must be between 0 and 1")
    
        # Initialize best approximation
        best_error = float("inf")
        best_numerator = 0
        best_denominator = 1
    
        for denominator in range(1, max_denominator + 1):
            numerator = round(x * denominator)
    
            error = abs(x - numerator / denominator)
    
            if error < best_error:
                best_numerator = numerator
                best_denominator = denominator
                best_error = error
    
        return best_numerator, best_denominator
  
    x = 1/3
    numerator, denominator = simple_approx(x)
    print(f"Best approximation for {x}: {numerator}/{denominator}")
    print(f"Decimal value: {numerator/denominator}")
    print(f"Error: {abs(x - numerator/denominator)}")  

But is there a better/quicker way?  It turns out that there is, using Farey sequences.  The code looks like this:

    def farey_approx(x, max_denominator=1000):
        # Ensure x is between 0 and 1
        if not 0 <= x <= 1:
            raise ValueError("x must be between 0 and 1")
    
        # Initialize lower and upper bounds
        a, b = 0, 1
        c, d = 1, 1
    
        while True:
            mediant_num = a + c
            mediant_den = b + d
    
            if mediant_den > max_denominator:
                break
    
            if x < mediant_num / mediant_den:
                c, d = mediant_num, mediant_den
            else:
                a, b = mediant_num, mediant_den
    
        # Determine which of the two bounds is the best approximation
        if abs(x - a/b) <= abs(x - c/d):
            return a, b
        else:
            return c, d

And it's significantly quicker:

    x = 0.605555555555
    start_time = time.time()
    numerator, denominator = simple_approx(x, 1000000)
    end_time = time.time()
    print("==Brute force==")
    print(f"Time taken: {round((end_time - start_time)*1000000, 2)} microseconds")
    print(f"Best approximation for {x}: {numerator}/{denominator}")
    print(f"Decimal value: {numerator/denominator}")
    print(f"Error: {abs(x - numerator/denominator)}")
    
    start_time = time.time()
    numerator, denominator = farey_approx(x, 1000000)
    end_time = time.time()
    print("==Farey sequence==")
    print(f"Time taken: {round((end_time - start_time)*1000000, 2)} microseconds")
    print(f"Best approximation for {x}: {numerator}/{denominator}")
    print(f"Decimal value: {numerator/denominator}")
    print(f"Error: {abs(x - numerator/denominator)}")

gives the following result on my desktop PC:

    ==Brute force==
    Time taken: 69877.39 microseconds
    Best approximation for 0.605555555555: 109/180
    Decimal value: 0.6055555555555555
    Error: 5.555556015224283e-13
    ==Farey sequence==
    Time taken: 1524.45 microseconds
    Best approximation for 0.605555555555: 109/180
    Decimal value: 0.6055555555555555
    Error: 5.555556015224283e-13

- so about 50x faster over the denominator range in question.

Is this important in the current application?  Probably not, as it's unlikely that we're going to be trying to alter the SI5351's PLL frequencies that often.  But it's neat.
