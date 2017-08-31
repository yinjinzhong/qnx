
test_func()
{
	index=`expr $count + 1`
	echo "		if (__prev) { \\"
	echo "			if (__malloc_bt_depth > $count) { \\"
	echo "				__my_builtin_return_address_n(__line, $index, __prev); \\"
	echo "				callerd[$index] = (unsigned *)__prev = __line; \\"
	echo "				__total++; \\"

	count=`expr $count + 1`
	if [ $count -lt 123 ]
	then
		test_func
	fi
	echo "			} /* $index */\\"
	echo "		} /* $index */\\"
}

test_func2()
{
	test_func
}

cat << EOF 
/* This file is generated do not edit */
#define MALLOC_GETBT(callerd) \\
{ \\
	int __line=0; \\
	int __prev=0; \\
	int __total=0; \\
	if ((callerd)) { \\
		__my_builtin_return_address_n(__line, 0, __prev); \\
		callerd[0] = (unsigned *)__prev = __line; \\
		__total++; \\
EOF

count=0
test_func

cat << EOF
	} \\
}
EOF
