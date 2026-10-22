## Location/Item ID Scheme
All item and location IDs follow the same 8-digit pattern, This pattern is as follows
G MM TT SSS  
G = a number pertaining to what game this item/location is for. For example if an id has a 1 in the G spot it is for haloCE  
MM = Mission Number, what mission this item/location is for. If the item does not map to a level then this stays 00  
TT = Type, the number to type mapping is game by game but for one game a type of 1 may map to a skull pickup location in a level  
SSS = An index number for that type. If we have a type mapping to skull locations and s = 1 then it would be the first skull in the level, 2 would be the second and so on. 

If an item is cross game its G field will be 0