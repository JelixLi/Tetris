?a
??
8
Const
output"dtype"
valuetensor"
dtypetype

NoOp
C
Placeholder
output"dtype"
dtypetype"
shapeshape:
@
ReadVariableOp
resource
value"dtype"
dtypetype?
?
StatefulPartitionedCall
args2Tin
output2Tout"
Tin
list(type)("
Tout
list(type)("	
ffunc"
configstring "
config_protostring "
executor_typestring ?
q
VarHandleOp
resource"
	containerstring "
shared_namestring "
dtypetype"
shapeshape?"serve*2.1.0-dev201911062v1.12.1-17437-g6517cee8?T
h

variable_xVarHandleOp*
_output_shapes
: *
dtype0*
shape: *
shared_name
variable_x
a
variable_x/Read/ReadVariableOpReadVariableOp
variable_x*
_output_shapes
: *
dtype0
h

variable_yVarHandleOp*
_output_shapes
: *
dtype0*
shape: *
shared_name
variable_y
a
variable_y/Read/ReadVariableOpReadVariableOp
variable_y*
_output_shapes
: *
dtype0
p
child_variableVarHandleOp*
_output_shapes
: *
dtype0*
shape: *
shared_namechild_variable
i
"child_variable/Read/ReadVariableOpReadVariableOpchild_variable*
_output_shapes
: *
dtype0
J
ConstConst*
_output_shapes
: *
dtype0*
valueB
 *  ?@

NoOpNoOp
?
Const_1Const"/device:CPU:0*
_output_shapes
: *
dtype0*?
value?B? B?
)
x
y
	child

signatures
<:
VARIABLE_VALUE
variable_xx/.ATTRIBUTES/VARIABLE_VALUE
<:
VARIABLE_VALUE
variable_yy/.ATTRIBUTES/VARIABLE_VALUE

z
 
FD
VARIABLE_VALUEchild_variable"child/z/.ATTRIBUTES/VARIABLE_VALUE
R
serving_default_aPlaceholder*
_output_shapes
: *
dtype0*
shape: 
R
serving_default_bPlaceholder*
_output_shapes
: *
dtype0*
shape: 
?
StatefulPartitionedCallStatefulPartitionedCallserving_default_aserving_default_b
variable_x
variable_ychild_variableConst*
Tin

2*
Tout
2*,
_gradient_op_typePartitionedCallUnused*
_output_shapes
: **
config_proto

GPU 

CPU2J 8*)
f$R"
 __inference_signature_wrapper_45
O
saver_filenamePlaceholder*
_output_shapes
: *
dtype0*
shape: 
?
StatefulPartitionedCall_1StatefulPartitionedCallsaver_filenamevariable_x/Read/ReadVariableOpvariable_y/Read/ReadVariableOp"child_variable/Read/ReadVariableOpConst_1*
Tin	
2*
Tout
2*,
_gradient_op_typePartitionedCallUnused*
_output_shapes
: **
config_proto

GPU 

CPU2J 8*$
fR
__inference__traced_save_80
?
StatefulPartitionedCall_2StatefulPartitionedCallsaver_filename
variable_x
variable_ychild_variable*
Tin
2*
Tout
2*,
_gradient_op_typePartitionedCallUnused*
_output_shapes
: **
config_proto

GPU 

CPU2J 8*(
f#R!
__inference__traced_restore_101?B
?
?
 __inference_signature_wrapper_45
a
b"
statefulpartitionedcall_args_2"
statefulpartitionedcall_args_3"
statefulpartitionedcall_args_4"
statefulpartitionedcall_args_5
identity??StatefulPartitionedCall?
StatefulPartitionedCallStatefulPartitionedCallabstatefulpartitionedcall_args_2statefulpartitionedcall_args_3statefulpartitionedcall_args_4statefulpartitionedcall_args_5*
Tin

2*
Tout
2*,
_gradient_op_typePartitionedCallUnused*
_output_shapes
: **
config_proto

GPU 

CPU2J 8*
fR
__inference_compute_342
StatefulPartitionedCall}
IdentityIdentity StatefulPartitionedCall:output:0^StatefulPartitionedCall*
T0*
_output_shapes
: 2

Identity"
identityIdentity:output:0*%
_input_shapes
: : :::: 22
StatefulPartitionedCallStatefulPartitionedCall:! 

_user_specified_namea:!

_user_specified_nameb
?
?
__inference_compute_34
a
b
add_readvariableop_resource!
add_1_readvariableop_resource#
truediv_readvariableop_resource
add_2_y
identity??add/ReadVariableOp?add_1/ReadVariableOp?truediv/ReadVariableOp|
add/ReadVariableOpReadVariableOpadd_readvariableop_resource*
_output_shapes
: *
dtype02
add/ReadVariableOpS
addAddV2aadd/ReadVariableOp:value:0*
T0*
_output_shapes
: 2
add?
add_1/ReadVariableOpReadVariableOpadd_1_readvariableop_resource*
_output_shapes
: *
dtype02
add_1/ReadVariableOpY
add_1AddV2badd_1/ReadVariableOp:value:0*
T0*
_output_shapes
: 2
add_1F
mulMuladd:z:0	add_1:z:0*
T0*
_output_shapes
: 2
mul?
truediv/ReadVariableOpReadVariableOptruediv_readvariableop_resource*
_output_shapes
: *
dtype02
truediv/ReadVariableOpg
truedivRealDivmul:z:0truediv/ReadVariableOp:value:0*
T0*
_output_shapes
: 2	
truedivN
add_2AddV2truediv:z:0add_2_y*
T0*
_output_shapes
: 2
add_2?
IdentityIdentity	add_2:z:0^add/ReadVariableOp^add_1/ReadVariableOp^truediv/ReadVariableOp*
T0*
_output_shapes
: 2

Identity"
identityIdentity:output:0*%
_input_shapes
: : :::: 2(
add/ReadVariableOpadd/ReadVariableOp2,
add_1/ReadVariableOpadd_1/ReadVariableOp20
truediv/ReadVariableOptruediv/ReadVariableOp:! 

_user_specified_namea:!

_user_specified_nameb
?
?
__inference__traced_save_80
file_prefix)
%savev2_variable_x_read_readvariableop)
%savev2_variable_y_read_readvariableop-
)savev2_child_variable_read_readvariableop
savev2_1_const_1

identity_1??MergeV2Checkpoints?SaveV2?SaveV2_1?
StringJoin/inputs_1Const"/device:CPU:0*
_output_shapes
: *
dtype0*<
value3B1 B+_temp_248049d02b7345e7bdb960f6d19f995b/part2
StringJoin/inputs_1?

StringJoin
StringJoinfile_prefixStringJoin/inputs_1:output:0"/device:CPU:0*
N*
_output_shapes
: 2

StringJoinZ

num_shardsConst*
_output_shapes
: *
dtype0*
value	B :2

num_shards
ShardedFilename/shardConst"/device:CPU:0*
_output_shapes
: *
dtype0*
value	B : 2
ShardedFilename/shard?
ShardedFilenameShardedFilenameStringJoin:output:0ShardedFilename/shard:output:0num_shards:output:0"/device:CPU:0*
_output_shapes
: 2
ShardedFilename?
SaveV2/tensor_namesConst"/device:CPU:0*
_output_shapes
:*
dtype0*s
valuejBhBx/.ATTRIBUTES/VARIABLE_VALUEBy/.ATTRIBUTES/VARIABLE_VALUEB"child/z/.ATTRIBUTES/VARIABLE_VALUE2
SaveV2/tensor_names?
SaveV2/shape_and_slicesConst"/device:CPU:0*
_output_shapes
:*
dtype0*
valueBB B B 2
SaveV2/shape_and_slices?
SaveV2SaveV2ShardedFilename:filename:0SaveV2/tensor_names:output:0 SaveV2/shape_and_slices:output:0%savev2_variable_x_read_readvariableop%savev2_variable_y_read_readvariableop)savev2_child_variable_read_readvariableop"/device:CPU:0*
_output_shapes
 *
dtypes
22
SaveV2?
ShardedFilename_1/shardConst"/device:CPU:0*
_output_shapes
: *
dtype0*
value	B :2
ShardedFilename_1/shard?
ShardedFilename_1ShardedFilenameStringJoin:output:0 ShardedFilename_1/shard:output:0num_shards:output:0"/device:CPU:0*
_output_shapes
: 2
ShardedFilename_1?
SaveV2_1/tensor_namesConst"/device:CPU:0*
_output_shapes
:*
dtype0*1
value(B&B_CHECKPOINTABLE_OBJECT_GRAPH2
SaveV2_1/tensor_names?
SaveV2_1/shape_and_slicesConst"/device:CPU:0*
_output_shapes
:*
dtype0*
valueB
B 2
SaveV2_1/shape_and_slices?
SaveV2_1SaveV2ShardedFilename_1:filename:0SaveV2_1/tensor_names:output:0"SaveV2_1/shape_and_slices:output:0savev2_1_const_1^SaveV2"/device:CPU:0*
_output_shapes
 *
dtypes
22

SaveV2_1?
&MergeV2Checkpoints/checkpoint_prefixesPackShardedFilename:filename:0ShardedFilename_1:filename:0^SaveV2	^SaveV2_1"/device:CPU:0*
N*
T0*
_output_shapes
:2(
&MergeV2Checkpoints/checkpoint_prefixes?
MergeV2CheckpointsMergeV2Checkpoints/MergeV2Checkpoints/checkpoint_prefixes:output:0file_prefix	^SaveV2_1"/device:CPU:0*
_output_shapes
 2
MergeV2Checkpointsr
IdentityIdentityfile_prefix^MergeV2Checkpoints"/device:CPU:0*
T0*
_output_shapes
: 2

Identity?

Identity_1IdentityIdentity:output:0^MergeV2Checkpoints^SaveV2	^SaveV2_1*
T0*
_output_shapes
: 2

Identity_1"!

identity_1Identity_1:output:0*
_input_shapes

: : : : : 2(
MergeV2CheckpointsMergeV2Checkpoints2
SaveV2SaveV22
SaveV2_1SaveV2_1:+ '
%
_user_specified_namefile_prefix
?
?
__inference__traced_restore_101
file_prefix
assignvariableop_variable_x!
assignvariableop_1_variable_y%
!assignvariableop_2_child_variable

identity_4??AssignVariableOp?AssignVariableOp_1?AssignVariableOp_2?	RestoreV2?RestoreV2_1?
RestoreV2/tensor_namesConst"/device:CPU:0*
_output_shapes
:*
dtype0*s
valuejBhBx/.ATTRIBUTES/VARIABLE_VALUEBy/.ATTRIBUTES/VARIABLE_VALUEB"child/z/.ATTRIBUTES/VARIABLE_VALUE2
RestoreV2/tensor_names?
RestoreV2/shape_and_slicesConst"/device:CPU:0*
_output_shapes
:*
dtype0*
valueBB B B 2
RestoreV2/shape_and_slices?
	RestoreV2	RestoreV2file_prefixRestoreV2/tensor_names:output:0#RestoreV2/shape_and_slices:output:0"/device:CPU:0* 
_output_shapes
:::*
dtypes
22
	RestoreV2X
IdentityIdentityRestoreV2:tensors:0*
T0*
_output_shapes
:2

Identity?
AssignVariableOpAssignVariableOpassignvariableop_variable_xIdentity:output:0*
_output_shapes
 *
dtype02
AssignVariableOp\

Identity_1IdentityRestoreV2:tensors:1*
T0*
_output_shapes
:2

Identity_1?
AssignVariableOp_1AssignVariableOpassignvariableop_1_variable_yIdentity_1:output:0*
_output_shapes
 *
dtype02
AssignVariableOp_1\

Identity_2IdentityRestoreV2:tensors:2*
T0*
_output_shapes
:2

Identity_2?
AssignVariableOp_2AssignVariableOp!assignvariableop_2_child_variableIdentity_2:output:0*
_output_shapes
 *
dtype02
AssignVariableOp_2?
RestoreV2_1/tensor_namesConst"/device:CPU:0*
_output_shapes
:*
dtype0*1
value(B&B_CHECKPOINTABLE_OBJECT_GRAPH2
RestoreV2_1/tensor_names?
RestoreV2_1/shape_and_slicesConst"/device:CPU:0*
_output_shapes
:*
dtype0*
valueB
B 2
RestoreV2_1/shape_and_slices?
RestoreV2_1	RestoreV2file_prefix!RestoreV2_1/tensor_names:output:0%RestoreV2_1/shape_and_slices:output:0
^RestoreV2"/device:CPU:0*
_output_shapes
:*
dtypes
22
RestoreV2_19
NoOpNoOp"/device:CPU:0*
_output_shapes
 2
NoOp?

Identity_3Identityfile_prefix^AssignVariableOp^AssignVariableOp_1^AssignVariableOp_2^NoOp"/device:CPU:0*
T0*
_output_shapes
: 2

Identity_3?

Identity_4IdentityIdentity_3:output:0^AssignVariableOp^AssignVariableOp_1^AssignVariableOp_2
^RestoreV2^RestoreV2_1*
T0*
_output_shapes
: 2

Identity_4"!

identity_4Identity_4:output:0*!
_input_shapes
: :::2$
AssignVariableOpAssignVariableOp2(
AssignVariableOp_1AssignVariableOp_12(
AssignVariableOp_2AssignVariableOp_22
	RestoreV2	RestoreV22
RestoreV2_1RestoreV2_1:+ '
%
_user_specified_namefile_prefix"?L
saver_filename:0StatefulPartitionedCall_1:0StatefulPartitionedCall_28"
saved_model_main_op

NoOp*>
__saved_model_init_op%#
__saved_model_init_op

NoOp*?
serving_default?

a
serving_default_a:0 

b
serving_default_b:0 +
output_0
StatefulPartitionedCall:0 tensorflow/serving/predict:?
T
x
y
	child

signatures
compute"
_generic_user_object
: 2
variable_x
: 2
variable_y
%
z"
_generic_user_object
,
serving_default"
signature_map
: 2child_variable
?2?
__inference_compute_34?
???
FullArgSpec
args?
jself
ja
jb
varargs
 
varkw
 
defaults
 

kwonlyargs? 
kwonlydefaults
 
annotations? *?
? 
? 
*B(
 __inference_signature_wrapper_45ab
	J
ConstP
__inference_compute_346%?"
?

?
a 

?
b 
? "? 
 __inference_signature_wrapper_45[/?,
? 
%?"

a
?
a 

b
?
b ""?

output_0?
output_0 