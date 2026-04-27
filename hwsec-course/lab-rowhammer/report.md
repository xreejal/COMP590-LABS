Credentials:
Username: group8
Password: oYWhDp2YOGLEUaRc
Connect on VPN if off-campus
Ssh group8@152.2.130.63
Password: oYWhDp2YOGLEUaRc
Use Cisco VPN if off campus

$ make clean
$ make 
$ ./launch.sh partX
Submitting job(s). 1 job(s) submitted to cluster XX. All jobs done.
$ cat log/partX.out 
virt_to_phys test failed!

Discussion 1-2
4KB pages
Offset: bits 0–11
Page number: bits 12–63
2MB pages
Offset: bits 0–20
Page number: bits 21–63
# of 2MB pages in 2GB
1024 pages

Discussion Question 2-1
Row ID: 0x4b76
Column ID: 0x1000
Attacker addresses:
16 addresses with:

 row = 0x4b77
column = 0x1000
same bank id (via XOR-invariant bit flips in bits 7–20)

Discussion Question 2-3
Around 400 cycles is a safe barrier between the faster samples of around 330, and the slower samples of around 470.



Discussion 3.2:


We look into k=3, this way  we look back at this

And notice that a14 and a17 must be the same, which means when changing a row address by 1, we need to also make sure A14 changes. For this reason we need to set A14 to 1. Since 14-16 are not used in the b0 calculation, we can ignore them. That means the only bank bit we have left is 13 which should output 0 in the end like when we tested the previous row. This means we should keep it unchanged and thus  13-16 should be 0011 or 3

4-2 Discussion Questions

Data Pattern(Victim/Aggresor)
0x00/0xff
0xff/0x00
0x00/0x00
0xff/0xff
Number of Flips (100 trials)
# of rows bit flips occur in
# total count
100
711
100
798
0
0
1
0


Yes, they match our expectations. Opposing patterns work better. With more tests, likely it will reveal flipping the victim from 0 to 1 works slightly better than the opposite. 

Meta comment: We have some confusion over if we are supposed to record if a bit flip occurred at all in the row at all or the total number. We count both in the table. 

5-1: ECC Comparison Table (data length = 4)


1-Repetition (No ECC)
2-Repetition
3-Repetition
Single Parity Bit
Hamming(7,4)
Code Rate (Data Bits / Total Bits)
4/4 = 1.0
4/8 = 0.5
4/12 = 0.33
4/5 = 0.8
4/7 ≈ 0.57
Max Errors Can Detect
0
1
2
1
2
Max Errors Can Correct
0
0
1
0
1

Reasoning:
2-Repetition: 1011 1011 — a single flip makes the two copies disagree, so you know an error occurred but can't tell which copy is correct.
3-Repetition: 1011 1011 1011 — majority vote corrects 1 error; if 2 bits flip in the same position across copies you can detect but not always correct.
Single Parity Bit: XOR of all bits detects any odd number of flips (so 1), but two flips cancel out and go undetected.
Hamming(7,4): Designed to always correct 1 error and detect 2.

5-3: Correcting a Single Bit Flip in Hamming(22,16)
When a single bit flip is detected (syndrome ≠ 0, overall parity = 1), the syndrome value directly encodes the position of the corrupted bit within the 22-bit encoded word. To correct the error, simply flip the bit at the position indicated by the syndrome back to its original value. Since only one bit changed, this restores the data to its original correct state.

5-5: Can Hamming(22,16) Always Protect Against Rowhammer?
No, it cannot always protect against Rowhammer. Here's why:
Hamming(22,16) is a SECDED code — it can correct only 1-bit errors and merely detect 2-bit errors (without correcting them). A clever attacker can defeat it in two ways:
Double-bit flip in the same word: If Rowhammer flips 2 bits within the same 22-bit protected word, the error is detected but uncorrectable — the system has no way to recover the original data.
Targeted double-bit flip: An attacker who knows the ECC layout can deliberately hammer two specific rows such that exactly 2 bits flip within the same codeword. Since the syndrome cannot distinguish which two bits flipped, the error cannot be corrected. The attacker could repeat this to cause data corruption that the ECC silently fails to fix.
Beyond double flips: If Rowhammer causes 3+ bit flips in a word, even detection is not guaranteed — the errors can cancel out in the parity check and appear as no error at all.
In practice, Rowhammer can reliably induce multiple bit flips with enough hammering iterations, making SECDED ECC an insufficient defense against a determined attacker.

