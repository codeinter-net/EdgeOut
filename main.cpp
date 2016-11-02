//---------------------------------------------------------------------------
// EdgeOut
// PBA 11/10/2006 - 14/02/2007 - 16-22-26/05/2008
// Suppresses dark or light edges dued to over-sharpened pictures
// Adds to the picture the difference between two gaussian blurs of different radius
// Useful for many camcorders that trys to artificially increase the sharpness of the picture
//---------------------------------------------------------------------------

// TODO :
// - mode entrelacé
// - valeurs limites des paramètres
// - rechercher pourquoi les rayons très petits produisent du bruit

#include <windows.h>
#include <stdio.h>
#include <math.h>

#include "include/ScriptInterpreter.h"
#include "include/ScriptError.h"
#include "include/ScriptValue.h"

#include "resource.h"
#include "include/filter.h"

#define SQRT2PI 2.5066282746310	// sqrt(2*PI)
#define MATRIXSIZE	16

///////////////////////////////////////////////////////////////////////////

int	InitProc(FilterActivation *fa, const FilterFunctions *ff);
void	deInitProc(FilterActivation *fa, const FilterFunctions *ff);
int	RunProc(const FilterActivation *fa, const FilterFunctions *ff);
int	StartProc(FilterActivation *fa, const FilterFunctions *ff);
int	EndProc(FilterActivation *fa, const FilterFunctions *ff);
long	ParamProc(FilterActivation *fa, const FilterFunctions *ff);
int	ConfigProc(FilterActivation *fa, const FilterFunctions *ff, HWND hwnd);
void	StringProc(const FilterActivation *fa, const FilterFunctions *ff, char *str);
void	ScriptConfig(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc);
bool	FssProc(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen);

///////////////////////////////////////////////////////////////////////////

typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char byte;

typedef struct FilterData
{
	long	Gxp;
	long	Gyp;
	long	Gxm;
	long	Gym;
	long	Dark;
	long	Light;

	short	Mxp[MATRIXSIZE][256];
	short	Myp[MATRIXSIZE][256];
	short	Mxm[MATRIXSIZE][256];
	short	Mym[MATRIXSIZE][256];

	long	DarkTable[512];
	long	LightTable[512];

	short*	Src16;
	short*	Tmp16;
	short*	Gbp16;
	short*	Gbm16;

//	Pixel32*	Temp;
//	Pixel32*	Temp2;

	bool	Inter;
	bool	ShowEdges;
} FilterData;

ScriptFunctionDef tutorial_func_defs[]={
    { (ScriptFunctionPtr)ScriptConfig, "Config", "0iiiiiii" },
    { NULL },
};

CScriptObject tutorial_obj={
    NULL, tutorial_func_defs
};

struct FilterDefinition FilterDef =
{
	NULL,NULL,NULL,			// next, prev, module
	"EdgeOut",					// name
	"Suppress edges dued to over-sharpened pictures", // description
	"PBA",
	NULL,							// private_data
	sizeof(FilterData),		// inst_data_size
	InitProc,					// initProc
	NULL,							// deinitProc
	RunProc,
	ParamProc,
	ConfigProc,
	StringProc,
	StartProc,
	EndProc,
	&tutorial_obj,
	FssProc,
};

///////////////////////////////////////////////////////////////////////////

extern "C" int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat);
extern "C" void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff);

static FilterDefinition *fd_tutorial;

///////////////////////////////////////////////////////////////////////////

int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat)
{
	if (!(fd_tutorial = ff->addFilter(fm, &FilterDef, sizeof(FilterDefinition))))
		return 1;

	vdfd_ver=VIRTUALDUB_FILTERDEF_VERSION;
	vdfd_compat=VIRTUALDUB_FILTERDEF_COMPATIBLE;

	return(0);
}

void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff)
{
	ff->removeFilter(fd_tutorial);
}

///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
// InitProc
// Called one time during the filter initialisation
// PBA 11/10/2006
//---------------------------------------------------------------------------

int InitProc(FilterActivation *fa, const FilterFunctions *ff)
{
	FilterData *mfd=(FilterData*)fa->filter_data;
	mfd->Gxp=10;
	mfd->Gyp=10;
	mfd->Gxm=5;
	mfd->Gym=5;
	mfd->Dark=70;
	mfd->Light=180;
	mfd->Inter=0;
	mfd->ShowEdges=0;
	mfd->Src16=0;
	mfd->Tmp16=0;
	mfd->Gbp16=0;
	mfd->Gbm16=0;
	return(0);
}

//---------------------------------------------------------------------------
// deInitProc
// Called one time during the filter initialisation
// PBA 22/05/2008
//---------------------------------------------------------------------------

void deInitProc(FilterActivation *fa, const FilterFunctions *ff)
{
	FilterData *mfd=(FilterData*)fa->filter_data;
}

//---------------------------------------------------------------------------
// CalcGaussCurve
// Calculation of the coefficients of the gaussian curve used for the gaussian blur
//---------------------------------------------------------------------------

void CalcGaussCurve(long RL,short M[MATRIXSIZE][256])
{
short		i,j;
double	ExpConst;
double	MultConst;
long		G;
double	R;

	R=(float)RL/10;

	ExpConst=-0.5/(double)(R*R);
	MultConst=65536/((double)R*SQRT2PI);
	for(i=0;i<MATRIXSIZE;i++)
	{
		G=(long)(MultConst*exp(ExpConst*(double)(i*i)));
		for(j=0;j<256;j++)
		{
			M[i][j]=((G*j)+32768)>>16;
		}
	}
}

//---------------------------------------------------------------------------
// StartProc
// Called before processing for allocating buffers and initialising data
// PBA 16/05/2008
//---------------------------------------------------------------------------

int StartProc(FilterActivation *fa, const FilterFunctions *ff)
{
long	i,TotalSize;

	FilterData *mfd=(FilterData*)fa->filter_data;

// Allocation of the 16 bit per color buffers
// MATRIXSIZE is used to preserve space outside the picture for the gaussian blur
// Using h*3 instead of h*2 to preserve space beetween the two fields in interlaced mode

	mfd->Src16=0;
	mfd->Tmp16=0;
	mfd->Gbp16=0;
	mfd->Gbm16=0;
	TotalSize=(fa->src.w+MATRIXSIZE*2)*(fa->src.h+MATRIXSIZE*3)*sizeof(short)*3;
	mfd->Src16=(short*)malloc(TotalSize);
	if(!mfd->Src16) return(-1);
	mfd->Tmp16=(short*)malloc(TotalSize);
	if(!mfd->Tmp16) return(-1);
	mfd->Gbp16=(short*)malloc(TotalSize);
	if(!mfd->Gbp16) return(-1);
	mfd->Gbm16=(short*)malloc(TotalSize);
	if(!mfd->Gbm16) return(-1);

	CalcGaussCurve(mfd->Gxp,mfd->Mxp);
	CalcGaussCurve(mfd->Gyp,mfd->Myp);
	CalcGaussCurve(mfd->Gxm,mfd->Mxm);
	CalcGaussCurve(mfd->Gym,mfd->Mym);

	for(i=0;i<512;i++)
	{
		mfd->DarkTable[i]=((i*mfd->Dark)+50)/100;
		mfd->LightTable[i]=((i*mfd->Light)+50)/100;
	}

	return(0);
}

//---------------------------------------------------------------------------
// Multiply and accumulate function
// Used for doing the gaussian blur
//---------------------------------------------------------------------------

inline void Mac(short* Src,short* Dst,long Offset,short *SubMatrix)
{
int	i;
short	R,G,B;
short* Src2;

	R=SubMatrix[*Src++];
	G=SubMatrix[*Src++];
	B=SubMatrix[*Src];
	Src2=Src;

	for(i=1;i<MATRIXSIZE;i++)
	{
		SubMatrix+=256;
		if(!SubMatrix[255])	// If zero then the rest of the matrix is also zero -> No more data
			break;
		Src+=Offset-2;
		R+=SubMatrix[*Src++];
		G+=SubMatrix[*Src++];
		B+=SubMatrix[*Src];
		Src2-=Offset+2;
		R+=SubMatrix[*Src2++];
		G+=SubMatrix[*Src2++];
		B+=SubMatrix[*Src2];
	}

	Dst[0]=R;
	Dst[1]=G;
	Dst[2]=B;
}

//---------------------------------------------------------------------------
// RunProc
// Main processing routine
// PBA 12/10/2006 - 14/02/2007
//---------------------------------------------------------------------------

int RunProc(const FilterActivation *fa,const FilterFunctions *ff)
{
long	i,TotalSize,OffsetY,OffsetMaxY;
PixDim w,h;
bool	Inter,ShowEdges;
PixOffset pitch,modulo;
long	R,G,B;
Pixel32	Pix;
Pixel32	*Src,*Dst;
short	*Src16,*Dst16,*Gbp16,*Gbm16;
long	*DarkTable;
long	*LightTable;

// Initializations
	FilterData *mfd=(FilterData*)fa->filter_data;
	w=fa->src.w;
	h=fa->src.h;
	pitch=fa->src.pitch;
	modulo=fa->src.modulo;
	TotalSize=(fa->src.w+MATRIXSIZE*2)*(fa->src.h+MATRIXSIZE);
	OffsetY=(fa->src.w+MATRIXSIZE*2)*3;
	OffsetMaxY=OffsetY*MATRIXSIZE;

// Parameters from the user interface
	Inter=mfd->Inter;
	ShowEdges=mfd->ShowEdges;

// Convert from Pixel32 to short[3]

	Src=(Pixel32*)fa->src.data;
	Dst16=mfd->Src16+(MATRIXSIZE+(fa->src.w+MATRIXSIZE*2)*MATRIXSIZE)*3;

	for(h=fa->src.h;--h>=0;)
	{
		for(w=fa->src.w;--w>=0;)
		{
			Pix=*Src++;
			*Dst16++=(short)((Pix>>16)&0xFF);
			*Dst16++=(short)((Pix>>8)&0xFF);
			*Dst16++=(short)(Pix&0xFF);
		}
		Src=(Pixel32*)((char*)(Src)+modulo);
		Dst16+=(MATRIXSIZE<<1)*3;
	}

// Horizontal positive blur : GXP (Src->Tmp)

	Src16=mfd->Src16+OffsetMaxY;
	Dst16=mfd->Tmp16+OffsetMaxY;
	for(i=TotalSize;--i>=0;)
	{
		Mac(Src16,Dst16,3,*mfd->Mxp);
		Src16+=3;
		Dst16+=3;
	}

// Vertical positive blur : GYP (Tmp->Gbp16)

	Src16=mfd->Tmp16+OffsetMaxY;
	Dst16=mfd->Gbp16+OffsetMaxY;
	for(i=TotalSize;--i>=0;)
	{
		Mac(Src16,Dst16,OffsetY,*mfd->Myp);
		Src16+=3;
		Dst16+=3;
	}

// Horizontal negative blur : GXM (Src->Tmp)

	Src16=mfd->Src16+OffsetMaxY;
	Dst16=mfd->Tmp16+OffsetMaxY;
	for(i=TotalSize;--i>=0;)
	{
		Mac(Src16,Dst16,3,*mfd->Mxm);
		Src16+=3;
		Dst16+=3;
	}

// Vertical negative blur : GYM (Tmp->Gbm16)

	Src16=mfd->Tmp16+OffsetMaxY;
	Dst16=mfd->Gbm16+OffsetMaxY;
	for(i=TotalSize;--i>=0;)
	{
		Mac(Src16,Dst16,OffsetY,*mfd->Mym);
		Src16+=3;
		Dst16+=3;
	}

// Final recombination : Source + Positive blur - Negative blur -> Destination

	Src16=mfd->Src16+(MATRIXSIZE+(fa->src.w+MATRIXSIZE*2)*MATRIXSIZE)*3;
	Gbp16=mfd->Gbp16+(MATRIXSIZE+(fa->src.w+MATRIXSIZE*2)*MATRIXSIZE)*3;
	Gbm16=mfd->Gbm16+(MATRIXSIZE+(fa->src.w+MATRIXSIZE*2)*MATRIXSIZE)*3;
	DarkTable=mfd->DarkTable;
	LightTable=mfd->LightTable;
	Dst=(Pixel32*)fa->dst.data;

	for(h=fa->dst.h;--h>=0;)
	{
		for(w=fa->dst.w;--w>=0;)
		{
			R=*(Gbp16++)-*(Gbm16++);	// Differences between the two blurs makes the edges appearing
			G=*(Gbp16++)-*(Gbm16++);
			B=*(Gbp16++)-*(Gbm16++);
			R=(R>0)?LightTable[R]:-DarkTable[-R];	// Modulation of the power of the dark and light edges
			G=(G>0)?LightTable[G]:-DarkTable[-G];
			B=(B>0)?LightTable[B]:-DarkTable[-B];
			if(!ShowEdges)
			{
				R+=*Src16++;	// Source picture
				G+=*Src16++;
				B+=*Src16++;
			}
			else
			{
				R+=128;			// Grey picture for the show edges mode
				G+=128;
				B+=128;
			}
			if(R<0) R=0;	// Peak limits
			if(G<0) G=0;
			if(B<0) B=0;
			if(R>255) R=255;
			if(G>255) G=255;
			if(B>255) B=255;
			*Dst++=(R<<16)|(G<<8)|B;
		}
		Src16+=(MATRIXSIZE<<1)*3;
		Gbp16+=(MATRIXSIZE<<1)*3;
		Gbm16+=(MATRIXSIZE<<1)*3;
		Dst=(Pixel32*)((char*)(Dst)+modulo);
	}

	return(0);
}

//---------------------------------------------------------------------------
// EndProc
// Called after processing for freeing buffers
// PBA 11/10/2006
//---------------------------------------------------------------------------

int EndProc(FilterActivation *fa,const FilterFunctions *ff)
{
	FilterData *mfd=(FilterData*)fa->filter_data;
	if(mfd->Src16) free(mfd->Src16);
	if(mfd->Tmp16) free(mfd->Tmp16);
	if(mfd->Gbp16) free(mfd->Gbp16);
	if(mfd->Gbm16) free(mfd->Gbm16);
	mfd->Src16=0;
	mfd->Tmp16=0;
	mfd->Gbp16=0;
	mfd->Gbm16=0;
	return(0);
}

//---------------------------------------------------------------------------
// ParamProc
// Filter parameters
// PBA 11/10/2006
//---------------------------------------------------------------------------

long ParamProc(FilterActivation *fa,const FilterFunctions *ff)
{
//	FilterData *mfd=(FilterData*)fa->filter_data;
	return(FILTERPARAM_SWAP_BUFFERS);
}

//---------------------------------------------------------------------------
// ConfigDlgProc
// Configuration dialog box input / output
// PBA 11/10/2006
//---------------------------------------------------------------------------

BOOL CALLBACK ConfigDlgProc(HWND hdlg,UINT msg,WPARAM wParam,LPARAM lParam)
{
	FilterData *mfd=(FilterData*)GetWindowLong(hdlg,DWL_USER);

	switch(msg)
	{
	case WM_INITDIALOG:
		SetWindowLong(hdlg,DWL_USER,lParam);
		mfd=(FilterData*)lParam;
		if(mfd)
		{
			SetDlgItemInt(hdlg,IDC_GXP,mfd->Gxp,0);
			SetDlgItemInt(hdlg,IDC_GYP,mfd->Gyp,0);
			SetDlgItemInt(hdlg,IDC_GXM,mfd->Gxm,0);
			SetDlgItemInt(hdlg,IDC_GYM,mfd->Gym,0);
			SetDlgItemInt(hdlg,IDC_DARK,mfd->Dark,0);
			SetDlgItemInt(hdlg,IDC_LIGHT,mfd->Light,0);
			CheckDlgButton(hdlg,IDC_INTER,mfd->Inter);
			CheckDlgButton(hdlg,IDC_EDGES,mfd->ShowEdges);
		}
		return(true);
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			if(mfd)
			{
				mfd->Gxp=GetDlgItemInt(hdlg,IDC_GXP,0,0);
				mfd->Gyp=GetDlgItemInt(hdlg,IDC_GYP,0,0);
				mfd->Gxm=GetDlgItemInt(hdlg,IDC_GXM,0,0);
				mfd->Gym=GetDlgItemInt(hdlg,IDC_GYM,0,0);
				mfd->Dark=GetDlgItemInt(hdlg,IDC_DARK,0,0);
				mfd->Light=GetDlgItemInt(hdlg,IDC_LIGHT,0,0);
				mfd->Inter=!!IsDlgButtonChecked(hdlg,IDC_INTER);
				mfd->ShowEdges=!!IsDlgButtonChecked(hdlg,IDC_EDGES);
			}
			EndDialog(hdlg,0);
			return(true);
		case IDCANCEL:
			EndDialog(hdlg,1);
			return(false);
		}
		break;
	}
	return(false);
}

//---------------------------------------------------------------------------
// ConfigProc
// Access to the configuration dialog box
// PBA 11/10/2006
//---------------------------------------------------------------------------

int ConfigProc(FilterActivation *fa,const FilterFunctions *ff,HWND hwnd)
{
	return(DialogBoxParam(fa->filter->module->hInstModule,MAKEINTRESOURCE(IDD_FILTER_TUTORIAL),hwnd,ConfigDlgProc,(LPARAM)fa->filter_data));
}

//---------------------------------------------------------------------------
// StringProc
// Display info in the filter list
// PBA 14/02/2007
//---------------------------------------------------------------------------

void StringProc(const FilterActivation *fa, const FilterFunctions *ff, char *str)
{
	FilterData *mfd=(FilterData*)fa->filter_data;
	_snprintf(str,80," (+[%ld,%ld] -[%ld,%ld] x[%ld,%ld]%s)",mfd->Gxp,mfd->Gyp,mfd->Gxm,mfd->Gym,mfd->Dark,mfd->Light,mfd->Inter?" I":"");
}

//---------------------------------------------------------------------------
// ScriptConfig
// Initialisation of filter parameters
// PBA 14/02/2007
//---------------------------------------------------------------------------

void ScriptConfig(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc)
{
	FilterActivation *fa=(FilterActivation*)lpVoid;
	FilterData *mfd=(FilterData*)fa->filter_data;
	mfd->Gxp=argv[0].asInt();
	mfd->Gyp=argv[1].asInt();
	mfd->Gxm=argv[2].asInt();
	mfd->Gym=argv[3].asInt();
	mfd->Dark=argv[4].asInt();
	mfd->Light=argv[5].asInt();
	mfd->Inter=!!argv[6].asInt();
}

//---------------------------------------------------------------------------
// FssProc
// Backup of filter parameters
// PBA 14/02/2007
//---------------------------------------------------------------------------

bool FssProc(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen)
{
	FilterData *mfd=(FilterData *)fa->filter_data;
	_snprintf(buf,buflen,"Config(%d,%d,%d,%d,%d,%d,%d)",mfd->Gxp,mfd->Gyp,mfd->Gxm,mfd->Gym,mfd->Dark,mfd->Light,(int)mfd->Inter);
	return(true);
}

